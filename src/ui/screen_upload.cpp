// Settings > Display > Your picture (UploadPage in design/src/screens/Settings.tsx):
// turns the upload server on and shows what to do on the computer

#include <Arduino.h>

#include "app/settings.h"
#include "net/upload_server.h"
#include "net/wifi.h"
#include "screens.h"
#include "theme.h"

namespace ui::screens {
namespace {

const uint8_t kIdleMinutes[] = {5, 15, 30, 60};
const char *const kIdleLabels[] = {"5 min", "15 min", "30 min", "1 h"};
constexpr int kIdleCount = sizeof(kIdleMinutes);

lv_obj_t *body;
uint32_t shownRevision;
bool shownRunning;
const char *shownOffline;  // offlineText() when the Start button was built
bool cancelled;  // this page cut a transfer: say so until the next one

// Updated in place while on
lv_obj_t *receiving, *receivingName, *bar, *barFill, *percentLabel, *waiting, *countdown;

void build();

const char *const kWifiConnecting = "Connecting to Wi-Fi…";
const char *const kWifiSetUp = "Connect to Wi-Fi first, in Settings > Wi-Fi.";

// Why Start can't be used yet, nullptr once online. With a network saved,
// Wi-Fi keeps trying to get back on it.
const char *offlineText() {
    switch (wifi::status()) {
        case wifi::Status::Connected: return nullptr;
        case wifi::Status::Connecting:
        case wifi::Status::Disconnected: return kWifiConnecting;
        case wifi::Status::Off: return "Wi-Fi is off: swipe down from the top to turn it on.";
        default: return kWifiSetUp;
    }
}

void buildOff() {
    // Someone on the Wi-Fi guessing the code stops it: say so, or it looks like it just went off
    if (const char *why = upload_server::stoppedWhy()) addText(body, why, &font_ui_22, theme::kRed);
    // The page's explanation: bigger than other hints, it's all there is to go on here
    lv_obj_t *intro = addHint(body, "Send your own pictures from a computer on the same Wi-Fi, to use as wallpaper, "
                                    "and edit the favourite stations there. The radio is reachable for that only "
                                    "while this is on.");
    lv_obj_set_style_text_font(intro, &font_ui_22, 0);
    addSection(body, "STOP AFTER NO ACTIVITY FOR");
    int selected = 1;
    for (int i = 0; i < kIdleCount; i++) {
        if (kIdleMinutes[i] == settings::get().uploadIdleMinutes) selected = i;
    }
    addChips(body, kIdleLabels, kIdleCount, 4, selected, false, 54, [](int i) {
        settings::get().uploadIdleMinutes = kIdleMinutes[i];
        settings::changed();
    });

    shownOffline = offlineText();
    lv_obj_t *start = addPill(body, LV_SYMBOL_UPLOAD "  Start", true, 58, [](lv_event_t *) {
        cancelled = false;
        upload_server::start();  // tick() rebuilds the page: not here, inside the button's own event
    });
    lv_obj_set_width(start, LV_PCT(100));
    lv_obj_set_style_margin_top(start, 10, 0);
    if (shownOffline) {
        lv_obj_add_state(start, LV_STATE_DISABLED);
        lv_obj_set_style_bg_opa(start, LV_OPA_40, LV_STATE_DISABLED);
        lv_obj_set_style_text_font(addHint(body, shownOffline), &font_ui_22, 0);
    }
}

void buildOn() {
    lv_obj_t *card = addCard(body, nullptr, true);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(card, 4, 0);
    addText(card, "1 · On a computer on this Wi-Fi, open", &font_ui_20, theme::kTextDim);
    addText(card, "diy-radio.local", &font_ui_30, theme::accent());
    // Some computers don't find .local names; the address always works
    const String ip = "or " + upload_server::address();
    addText(card, ip.c_str(), &font_ui_20, theme::kTextDim);

    card = addCard(body, nullptr, true);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(card, 4, 0);
    addText(card, "2 · Type this code there", &font_ui_20, theme::kTextDim);
    lv_obj_t *code = addText(card, upload_server::code(), &font_ui_50, theme::kText);
    lv_obj_set_style_text_letter_space(code, 12, 0);

    // A picture on its way: name, progress, and × to cut it
    receiving = addCard(body, nullptr, true);
    lv_obj_set_flex_align(receiving, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(receiving, 8, 0);
    receivingName = addText(receiving, "", &font_ui_22, theme::kText);
    lv_obj_t *row = addRow(receiving, 14);
    bar = lv_obj_create(row);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, 0, 10);
    lv_obj_set_flex_grow(bar, 1);
    lv_obj_set_style_radius(bar, 5, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(theme::kSurfaceHi), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(bar, true, 0);
    barFill = lv_obj_create(bar);
    lv_obj_remove_style_all(barFill);
    lv_obj_set_size(barFill, 0, LV_PCT(100));
    lv_obj_set_style_bg_color(barFill, lv_color_hex(theme::accent()), 0);
    lv_obj_set_style_bg_opa(barFill, LV_OPA_COVER, 0);
    addRoundButton(row, 42, LV_SYMBOL_CLOSE, false, [](lv_event_t *) {
        cancelled = true;
        upload_server::cancelTransfer();
    });
    percentLabel = addText(receiving, "", &font_ui_20, theme::kTextDim);

    waiting = addRow(body, 12);
    lv_obj_set_style_pad_all(waiting, 8, 0);
    addText(waiting, "", &font_ui_22, theme::kTextDim);

    lv_obj_t *footer = addRow(body, 16);
    lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(footer, 8, 0);
    lv_obj_set_style_pad_top(footer, 4, 0);
    lv_obj_t *times = addColumn(footer, 2);
    lv_obj_set_width(times, LV_SIZE_CONTENT);
    countdown = addText(times, "", &font_ui_22, theme::kText);
    char after[40];
    const uint8_t minutes = settings::get().uploadIdleMinutes;
    snprintf(after, sizeof(after), minutes < 60 ? "%u min after the last activity" : "1 h after the last activity",
             minutes);
    addText(times, after, &font_ui_20, theme::kTextDim);
    lv_obj_t *stop = addPill(footer, "Stop", false, 50, [](lv_event_t *) {
        upload_server::stop();  // tick() rebuilds the page
    });
    lv_obj_set_width(stop, 140);
}

void tick(lv_timer_t *) {
    const bool running = upload_server::running();
    if (running != shownRunning) return build();
    if (!running) {
        if (offlineText() != shownOffline) build();  // Wi-Fi came up (or went): Start follows
        return;
    }

    const upload_server::Transfer t = upload_server::transfer();
    if (t.active) cancelled = false;
    lv_obj_set_hidden(receiving, !t.active);
    lv_obj_set_hidden(waiting, t.active);
    char buf[64];
    if (t.active) {
        snprintf(buf, sizeof(buf), "Receiving %s", t.name.c_str());
        setTextIfChanged(receivingName, buf);
        // A style setter redraws even when nothing changes, and this runs every 250 ms
        if (lv_obj_get_style_width(barFill, LV_PART_MAIN) != LV_PCT(t.percent)) {
            lv_obj_set_width(barFill, LV_PCT(t.percent));
        }
        snprintf(buf, sizeof(buf), "%d%% · it shows up under Wallpaper when done", t.percent);
        setTextIfChanged(percentLabel, buf);
    } else {
        setTextIfChanged(lv_obj_get_child(waiting, 0), cancelled ? LV_SYMBOL_CLOSE "  Cancelled. The computer can send it again."
                                                                 : LV_SYMBOL_REFRESH "  Waiting for your computer…");
    }
    const uint32_t left = upload_server::secondsLeft();
    snprintf(buf, sizeof(buf), "Stops in %lu:%02lu", left / 60, left % 60);
    setTextIfChanged(countdown, buf);
}

void build() {
    lv_obj_clean(body);
    shownRunning = upload_server::running();
    shownRevision = upload_server::revision();
    if (shownRunning) {
        buildOn();
        tick(nullptr);
    } else {
        buildOff();
    }
}

}  // namespace

void openUploadPage(lv_obj_t *screen) {
    body = openSubpage(screen, "Upload pictures", nullptr, "SETTINGS");
    cancelled = false;
    build();
    every(body, 250, tick);
}

}  // namespace ui::screens
