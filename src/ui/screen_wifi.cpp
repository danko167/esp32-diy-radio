// Settings > Wi-Fi: on or off, the current connection, the saved networks
// (up to 5, each with its own Forget), nearby networks, join with a password
// (design/src/screens/Settings.tsx, WiFi)

#include <Arduino.h>

#include <vector>

#include "net/wifi.h"
#include "screens.h"
#include "theme.h"

namespace ui::screens {
namespace {

constexpr uint32_t kRescanMs = 15000;

lv_obj_t *onSwitch;
lv_obj_t *offHint;
lv_obj_t *onPart;  // everything under the switch, hidden while Wi-Fi is off
lv_obj_t *statusIcon;
lv_obj_t *statusTitle;
lv_obj_t *statusName;
lv_obj_t *statusDetail;
lv_obj_t *savedPart;  // the section, its cards and the hint: hidden with none saved
lv_obj_t *savedTitle;
lv_obj_t *savedList;
lv_obj_t *spinner;
lv_obj_t *refresh;
lv_obj_t *list;
std::vector<wifi::Network> networks;
String shownSaved;  // what the saved cards show, to know when to build them again
String joinSsid;
uint32_t lastScan = 0;
bool scanning = false;
int shownConnected = -1;  // updateStatus()'s colours, -1 before the first

const char *statusText(wifi::Status s) {
    switch (s) {
        case wifi::Status::Connected: return "Connected";
        case wifi::Status::Connecting: return "Connecting…";
        case wifi::Status::WrongPassword: return "Wrong password";
        case wifi::Status::NotFound: return "Network not found";
        case wifi::Status::Off: return "Off";
        case wifi::Status::Disconnected: return "Not connected";
        default: return "Not set up";
    }
}

// Rough bars from RSSI, shown by dimming the Wi-Fi symbol
lv_opa_t signalOpacity(int32_t rssi) {
    if (rssi > -60) return LV_OPA_COVER;
    if (rssi > -70) return LV_OPA_80;
    if (rssi > -80) return LV_OPA_50;
    return LV_OPA_30;
}

const char *signalWord(int32_t rssi) {
    if (rssi > -60) return "strong";
    if (rssi > -70) return "good";
    if (rssi > -80) return "weak";
    return "very weak";
}

void updateStatus() {
    const wifi::Status s = wifi::status();
    const String ssid = wifi::ssid();
    const bool connected = s == wifi::Status::Connected;
    setTextIfChanged(statusTitle, statusText(s));
    setTextIfChanged(statusName, ssid.isEmpty() ? "Pick a network below" : ssid.c_str());
    // A style setter redraws even when nothing changes, and this runs every 500 ms
    if (connected != shownConnected) {
        shownConnected = connected;
        lv_obj_set_style_text_color(statusTitle, lv_color_hex(connected ? theme::accent() : theme::kTextDim), 0);
        lv_obj_set_style_bg_color(statusIcon, lv_color_hex(connected ? theme::accent() : theme::kSurfaceHi), 0);
        lv_obj_set_style_bg_opa(statusIcon, connected ? LV_OPA_20 : LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(statusIcon, 0),
                                    lv_color_hex(connected ? theme::accent() : theme::kTextDim), 0);
    }

    char detail[80] = "";
    if (connected) {
        const int32_t rssi = wifi::rssi();
        snprintf(detail, sizeof(detail), "IP %s · %s signal (%ld dBm)", wifi::ip().c_str(), signalWord(rssi),
                 (long)rssi);
    } else if (s == wifi::Status::WrongPassword) {
        snprintf(detail, sizeof(detail), "Tap the network to try again");
    }
    setTextIfChanged(statusDetail, detail);
    lv_obj_set_hidden(statusDetail, detail[0] == '\0');
}

bool inRange(const String &ssid) {
    for (const auto &n : networks) {
        if (n.ssid == ssid) return true;
    }
    return false;
}

void rebuildSaved();

void onForget(lv_event_t *e) {
    const String *name = static_cast<const String *>(lv_event_get_user_data(e));
    const String ssid = *name;  // the card (and this) goes with the rebuild
    wifi::forget(ssid);
    lv_async_call([](void *) { if (savedList) rebuildSaved(); }, nullptr);
}

// Only when something on them changed: names, which one is connected, which are in range
void rebuildSaved() {
    const std::vector<String> names = wifi::savedNetworks();
    const bool connected = wifi::status() == wifi::Status::Connected;
    const String current = connected ? wifi::ssid() : String();
    String key = current + "|";
    for (const auto &n : names) key += n + (inRange(n) ? "+" : "-") + "|";
    if (key == shownSaved) return;
    shownSaved = key;

    lv_obj_set_hidden(savedPart, names.empty());
    char title[40];
    snprintf(title, sizeof(title), "SAVED NETWORKS (%u OF %d)", (unsigned)names.size(), wifi::kMaxSaved);
    setTextIfChanged(lv_obj_get_child(savedTitle, 0), title);
    lv_obj_clean(savedList);
    static String forgetNames[wifi::kMaxSaved];
    for (size_t i = 0; i < names.size() && i < wifi::kMaxSaved; i++) {
        forgetNames[i] = names[i];
        const bool isCurrent = names[i] == current;
        lv_obj_t *card = addCard(savedList, nullptr, false);
        lv_obj_t *texts = addColumn(card, 2);
        lv_obj_set_flex_grow(texts, 1);
        lv_obj_set_width(texts, 0);  // shrink to what's left, so long names get dots
        lv_obj_t *name = addText(texts, names[i].c_str(), &font_ui_22, isCurrent ? theme::accent() : theme::kText);
        lv_obj_set_width(name, LV_PCT(100));
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        addText(texts, isCurrent ? "Connected" : inRange(names[i]) ? "In range" : "Not in range", &font_ui_20,
                theme::kTextDim);
        addRoundButton(card, 44, LV_SYMBOL_TRASH, false, onForget, &forgetNames[i]);
    }
}

void showOnOff() {
    const bool on = wifi::enabled();
    // The control centre may have changed it meanwhile
    if (lv_obj_has_state(onSwitch, LV_STATE_CHECKED) != on) lv_obj_set_state(onSwitch, LV_STATE_CHECKED, on);
    lv_obj_set_hidden(offHint, on);
    lv_obj_set_hidden(onPart, !on);
}

void join(const String &ssid, const String &password) {
    wifi::connect(ssid, password);
    updateStatus();
}

void onNetworkTap(lv_event_t *e) {
    const size_t index = (size_t)(intptr_t)lv_event_get_user_data(e);
    if (index >= networks.size()) return;
    const wifi::Network &n = networks[index];
    if (wifi::connectSaved(n.ssid)) {  // its password is known
        updateStatus();
        return;
    }
    if (!n.secured) {
        join(n.ssid, "");
        return;
    }
    joinSsid = n.ssid;
    char title[64];
    snprintf(title, sizeof(title), "Password for %s", n.ssid.c_str());
    openTextInput(lv_obj_get_screen(list), title, true,
                  [](const char *password) { join(joinSsid, password); });
}

void rebuildList() {
    lv_obj_clean(list);
    if (networks.empty()) {
        lv_obj_t *none = lv_label_create(list);
        lv_label_set_text(none, "No networks found");
        lv_obj_set_style_text_color(none, lv_color_hex(theme::kTextDim), 0);
        lv_obj_set_style_pad_left(none, 8, 0);
        return;
    }
    for (size_t i = 0; i < networks.size(); i++) {
        const wifi::Network &n = networks[i];
        lv_obj_t *row = makeTappable(addCard(list, nullptr, false), onNetworkTap, (void *)(intptr_t)i);

        lv_obj_t *name = lv_label_create(row);
        lv_label_set_text(name, n.ssid.c_str());
        if (wifi::status() == wifi::Status::Connected && n.ssid == wifi::ssid()) {
            lv_obj_set_style_text_color(name, lv_color_hex(theme::accent()), 0);
        }
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_flex_grow(name, 1);

        if (n.secured) {
            lv_obj_t *lock = lv_label_create(row);
            lv_label_set_text(lock, SYM_LOCK);
            lv_obj_set_style_text_color(lock, lv_color_hex(theme::kTextDim), 0);
        }
        lv_obj_t *signal = lv_label_create(row);
        lv_label_set_text(signal, LV_SYMBOL_WIFI);
        lv_obj_set_style_text_opa(signal, signalOpacity(n.rssi), 0);
    }
}

void tick(lv_timer_t *) {
    showOnOff();
    if (!wifi::enabled()) {
        scanning = false;
        return;
    }
    updateStatus();
    rebuildSaved();
    if (scanning && wifi::scanResults(networks)) {
        scanning = false;
        lv_obj_set_hidden(spinner, true);
        lv_obj_set_hidden(refresh, false);
        rebuildList();
        rebuildSaved();
    }
    if (!scanning && millis() - lastScan > kRescanMs) {
        wifi::startScan();
        scanning = true;
        lastScan = millis();
        lv_obj_set_hidden(spinner, false);
        lv_obj_set_hidden(refresh, true);
    }
}

}  // namespace

void openWifiPage(lv_obj_t *screen, void (*onClose)()) {
    lv_obj_t *body = openSubpage(screen, "Wi-Fi", onClose, "SETTINGS");
    lv_obj_add_event_cb(
        body, [](lv_event_t *) { savedList = nullptr; }, LV_EVENT_DELETE, nullptr);

    lv_obj_t *switchRow = addCard(body, "Wi-Fi", false);
    onSwitch = addSwitch(switchRow, wifi::enabled(), [](lv_event_t *e) {
        setWifiEnabled(lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED));
        shownSaved = "";
        lastScan = millis() - kRescanMs - 1;  // look right away when it's back on
        tick(nullptr);
    });
    offHint = addHint(body, "Off: no internet radio, weather or time from the internet. The saved networks stay, "
                            "and one joins again when Wi-Fi is back on. Also in the control centre: swipe down "
                            "from the top.");

    onPart = addColumn(body, 0);
    lv_obj_set_flex_flow(onPart, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(onPart, lv_obj_get_style_pad_row(body, LV_PART_MAIN), 0);
    body = onPart;  // the rest goes in it

    lv_obj_t *status = addCard(body, nullptr, true);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(status, 14, 0);
    lv_obj_t *top = addRow(status, 16);
    statusIcon = addIconCircle(top, LV_SYMBOL_WIFI, 66, theme::accent(), LV_OPA_20, theme::accent());
    lv_obj_set_style_text_font(lv_obj_get_child(statusIcon, 0), &font_ui_30, 0);
    lv_obj_t *texts = addColumn(top, 0);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_width(texts, 0);  // shrink to what's left, so long names get dots
    statusTitle = addText(texts, "", &font_ui_20, theme::accent());
    statusName = addText(texts, "", &font_ui_30, theme::kText);
    lv_obj_set_width(statusName, LV_PCT(100));
    lv_label_set_long_mode(statusName, LV_LABEL_LONG_DOT);
    statusDetail = addText(status, "", &font_ui_20, theme::kTextDim);

    savedPart = addColumn(body, 0);
    lv_obj_set_style_pad_row(savedPart, lv_obj_get_style_pad_row(body, LV_PART_MAIN), 0);
    savedTitle = addRow(savedPart, 0);
    addSection(savedTitle, "SAVED NETWORKS");
    savedList = addColumn(savedPart, 10);
    addHint(savedPart, "The strongest saved network in range joins by itself. A sixth replaces the one used "
                       "longest ago.");

    lv_obj_t *header = addRow(body, 0);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_right(header, 12, 0);
    addSection(header, "NETWORKS NEARBY");
    spinner = lv_spinner_create(header);
    lv_obj_set_size(spinner, 22, 22);
    lv_obj_set_style_arc_width(spinner, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, 3, LV_PART_INDICATOR);
    // Tap to look again
    refresh = addText(header, LV_SYMBOL_REFRESH, &font_ui_22, theme::kTextDim);
    lv_obj_set_hidden(refresh, true);
    lv_obj_set_clickable(refresh, true);
    lv_obj_set_ext_click_area(refresh, 16);
    lv_obj_add_event_cb(
        refresh, [](lv_event_t *) { lastScan = millis() - kRescanMs - 1; }, LV_EVENT_CLICKED, nullptr);

    list = lv_obj_create(body);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 10, 0);

    networks.clear();
    scanning = false;
    shownConnected = -1;
    shownSaved = "";
    lastScan = millis() - kRescanMs - 1;  // scan right away
    tick(every(list, 500, tick));
}

}  // namespace ui::screens
