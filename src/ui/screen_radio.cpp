// Internet radio, as designed in design/src/screens/InternetRadio.tsx:
// - Now Playing: big station and song, previous/next favourite, volume, sleep
// - Stations: favourites as numbered preset rows, popular, recent and search
// - Recently heard: songs from the stations' stream titles

#include <Arduino.h>

#include <vector>

#include "app/radio.h"
#include "audio/player.h"
#include "hal/pins.h"
#include "net/stations.h"
#include "net/wifi.h"
#include "screens.h"
#include "theme.h"

namespace ui::screens {
namespace {

// ----- Shared bits -----

// What's playing or about to: the player's station if it's a stream, else the last one
bool currentStation(stations::Station &out) { return stations::last(out); }

bool isCurrent(const stations::Station &s) {
    stations::Station cur;
    return currentStation(cur) && cur.url == s.url;
}

String detailText(const stations::Station &s) {
    String d = s.country;
    if (!s.codec.isEmpty()) d += (d.isEmpty() ? "" : " · ") + s.codec;
    if (s.bitrate) d += String(d.isEmpty() ? "" : " · ") + s.bitrate + " kbps";
    return d;
}

// ----- Now Playing -----

lv_obj_t *npName, *npTitle, *npStatus, *npPlayIcon, *npStar;
lv_obj_t *npVolume, *npVolumeIcon, *npPercent, *npSleep;
uint32_t npShownPlayer = UINT32_MAX;

void openStations(lv_obj_t *screen);
void openSongs(lv_obj_t *screen);

void updateNowPlaying() {
    const player::Status st = player::status();
    const bool radio = strncmp(st.source, "http", 4) == 0;
    stations::Station cur;
    const bool have = currentStation(cur);

    setTextIfChanged(npName, have ? cur.name.c_str() : "Pick a station");
    setTextIfChanged(npTitle, radio && st.state == player::State::Playing ? st.title : "");

    char status[64];
    const player::State state = radio ? st.state : player::State::Stopped;
    switch (state) {
        case player::State::Connecting:
            snprintf(status, sizeof(status), "Connecting…");
            break;
        case player::State::Playing: {
            const String codec = have ? cur.codec : String();
            const uint32_t kbps = st.bitrate ? st.bitrate / 1000 : (have ? cur.bitrate : 0);
            snprintf(status, sizeof(status), "Playing%s%s%s", codec.isEmpty() ? "" : " · ",
                     codec.c_str(), kbps ? (" · " + String(kbps) + " kbps").c_str() : "");
            break;
        }
        case player::State::Failed:
            snprintf(status, sizeof(status), "Couldn't play this station");
            break;
        default:
            if (player::held()) {
                snprintf(status, sizeof(status), "%s", player::held());
            } else {
                snprintf(status, sizeof(status), have ? "Stopped · tap play to listen" : "Open Stations to choose one");
            }
    }
    setTextIfChanged(npStatus, status);

    const bool active = state == player::State::Playing || state == player::State::Connecting;
    setTextIfChanged(npPlayIcon, active ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
    const bool favourite = have && stations::isFavourite(cur.url);
    lv_obj_set_style_text_color(npStar, lv_color_hex(favourite ? theme::accent() : theme::kTextDim), 0);
}

void updateVolume() {
    const int v = radio::volume();
    if (lv_slider_get_value(npVolume) != v && !lv_obj_has_state(npVolume, LV_STATE_PRESSED)) {
        lv_slider_set_value(npVolume, v, LV_ANIM_OFF);
    }
    char text[8];
    snprintf(text, sizeof(text), "%d%%", v);
    setTextIfChanged(npPercent, text);
    setTextIfChanged(npVolumeIcon, v == 0 ? LV_SYMBOL_MUTE : v < 50 ? LV_SYMBOL_VOLUME_MID : LV_SYMBOL_VOLUME_MAX);
}

void updateSleep() {
    const int left = radio::sleepMinutesLeft();
    char text[24];
    if (left) {
        snprintf(text, sizeof(text), SYM_MOON "  %d min", left);
    } else {
        snprintf(text, sizeof(text), SYM_MOON "  Sleep");
    }
    // A style setter redraws even when nothing changes, and this runs every 300 ms
    if (setTextIfChanged(npSleep, text)) {
        lv_obj_set_style_text_color(npSleep, lv_color_hex(left ? theme::accent() : theme::kText), 0);
    }
}

lv_obj_t *pill(lv_obj_t *parent, const char *label, lv_event_cb_t onTap) {
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_height(btn, 58);
    lv_obj_set_style_radius(btn, 28, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(theme::kSurfaceHi), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, onTap, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, label);
    lv_obj_center(l);
    return l;
}

void buildNowPlaying(lv_obj_t *body) {
    lv_obj_set_scrollable(body, false);  // fixed layout, nothing scrolls
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_hor(body, 20, 0);

    npStar = lv_obj_get_child(addHeaderButton(body, SYM_STAR,
                                              [](lv_event_t *) {
                                                  stations::Station cur;
                                                  if (currentStation(cur)) stations::toggleFavourite(cur);
                                                  updateNowPlaying();
                                              }),
                              0);

    lv_obj_t *info = addColumn(body, 6);
    npName = addText(info, "", &font_ui_50, theme::kText);
    lv_obj_set_width(npName, LV_PCT(100));
    lv_label_set_long_mode(npName, LV_LABEL_LONG_DOT);
    npTitle = addText(info, "", &font_ui_30, theme::accent());
    lv_obj_set_width(npTitle, LV_PCT(100));
    lv_obj_set_style_min_height(npTitle, 34, 0);
    lv_label_set_long_mode(npTitle, LV_LABEL_LONG_SCROLL_CIRCULAR);
    npStatus = addText(info, "", &font_ui_20, theme::kTextDim);

    lv_obj_t *controls = addRow(body, 40);
    lv_obj_set_flex_align(controls, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *prev = addRoundButton(controls, 66, LV_SYMBOL_PREV, false, [](lv_event_t *) { radio::step(-1); });
    lv_obj_t *play = addRoundButton(controls, 98, LV_SYMBOL_PLAY, true, [](lv_event_t *) {
        radio::togglePlay();
    });
    lv_obj_set_style_text_font(play, &font_ui_30, 0);
    npPlayIcon = lv_obj_get_child(play, 0);
    lv_obj_t *next = addRoundButton(controls, 66, LV_SYMBOL_NEXT, false, [](lv_event_t *) { radio::step(1); });
    // Off-white icons here, unlike the theme's white on other grey buttons
    for (lv_obj_t *btn : {prev, next}) lv_obj_set_style_text_color(btn, lv_color_hex(theme::kText), 0);

    lv_obj_t *volume = addRow(body, 16);
    npVolumeIcon = addText(volume, LV_SYMBOL_VOLUME_MID, &font_ui_22, theme::kTextDim);
    npVolume = lv_slider_create(volume);
    lv_obj_set_flex_grow(npVolume, 1);
    lv_slider_set_range(npVolume, 0, 100);
    lv_slider_set_value(npVolume, radio::volume(), LV_ANIM_OFF);
    lv_obj_set_gesture_bubble(npVolume, false);
    lv_obj_add_event_cb(
        npVolume,
        [](lv_event_t *e) {
            radio::setVolume(lv_slider_get_value(lv_event_get_target_obj(e)));
            updateVolume();
        },
        LV_EVENT_VALUE_CHANGED, nullptr);
    npPercent = addText(volume, "", &font_ui_22, theme::kTextDim);
    lv_obj_set_width(npPercent, 54);
    lv_obj_set_style_text_align(npPercent, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *actions = addRow(body, 8);
    pill(actions, LV_SYMBOL_LIST "  Stations",
         [](lv_event_t *e) { openStations(lv_obj_get_screen(lv_event_get_target_obj(e))); });
    pill(actions, SYM_HISTORY "  Songs",
         [](lv_event_t *e) { openSongs(lv_obj_get_screen(lv_event_get_target_obj(e))); });
    npSleep = pill(actions, "", [](lv_event_t *) {
        radio::cycleSleep();
        updateSleep();
    });

    npShownPlayer = UINT32_MAX;
    updateNowPlaying();
    updateVolume();
    updateSleep();
}

void tickNowPlaying() {
    if (player::revision() != npShownPlayer) {
        npShownPlayer = player::revision();
        updateNowPlaying();
    }
    updateVolume();  // the buttons change it too
    updateSleep();   // counts down
}

// ----- Stations -----

enum class Tab { Favourites, Popular, Recent, Search };

lv_obj_t *stTabs[4];
lv_obj_t *stList;
lv_obj_t *miniName, *miniTitle, *miniPlayIcon;
Tab stTab = Tab::Favourites;
Tab resultsFor = Tab::Favourites;  // which tab the directory results belong to
std::vector<stations::Station> shown;
std::vector<lv_obj_t *> shownRows;  // one for each of `shown`, in its style: presets() or stationRow()
bool rowsArePresets = false;
stations::State shownState = stations::State::Idle;
uint32_t stShownStations = UINT32_MAX;
uint32_t stShownPlayer = UINT32_MAX;
const char *stShownOffline = nullptr;  // offlineText() when the list was built
String lastQuery;

void rebuildStations();
void selectTab(Tab t);

// A hint with tighter lines
void message(lv_obj_t *parent, const char *s) { addHint(parent, s, 0); }

void hint() { message(stList, "Hold a station to add it to favourites."); }

// Stand-in for a logo: initials on a colour picked from the name
lv_obj_t *badge(lv_obj_t *parent, const String &name, int32_t size) {
    static const uint32_t kColours[] = {theme::kOrange, theme::kGreen, theme::kPurple,
                                        theme::kPink, theme::kCyan, theme::kYellow};
    uint32_t hash = 0;
    for (const char c : name) hash += (uint8_t)c;
    const uint32_t colour = kColours[hash % (sizeof(kColours) / sizeof(kColours[0]))];

    String initials;
    int words = 0;
    bool startOfWord = true;
    for (const char c : name) {
        if (c == ' ') {
            startOfWord = true;
        } else if (startOfWord && words < 2 && (uint8_t)c < 0x80) {
            initials += (char)toupper(c);
            words++;
            startOfWord = false;
        } else {
            startOfWord = false;
        }
    }

    lv_obj_t *b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, size, size);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(colour), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_20, 0);
    lv_obj_set_clickable(b, false);
    lv_obj_t *l = addText(b, initials.c_str(), &font_ui_20, colour);
    lv_obj_center(l);
    return b;
}

void onStationTap(lv_event_t *e) {
    const size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
    if (i < shown.size()) radio::play(shown[i]);
}

// Holding a station stars it, so there's no star button to hit by accident
void onStationHold(lv_event_t *e) {
    const size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
    if (i < shown.size()) stations::toggleFavourite(shown[i]);
}

void stationRow(size_t i) {
    const stations::Station &s = shown[i];
    lv_obj_t *row = addCard(stList, nullptr, false);
    lv_obj_set_clickable(row, true);
    lv_obj_set_style_bg_color(row, lv_color_hex(theme::kSurfaceHi), LV_STATE_PRESSED);
    lv_obj_set_style_pad_top(row, 8, 0);
    lv_obj_set_style_pad_bottom(row, 8, 0);
    lv_obj_set_style_pad_left(row, 8, 0);
    lv_obj_set_style_pad_column(row, 14, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(row, onStationTap, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);
    lv_obj_add_event_cb(row, onStationHold, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)i);

    badge(row, s.name, 40);
    lv_obj_t *col = addColumn(row, 2);
    lv_obj_set_width(col, 0);
    lv_obj_set_flex_grow(col, 1);
    lv_obj_t *name = addText(col, s.name.c_str(), &font_ui_22, isCurrent(s) ? theme::accent() : theme::kText);
    lv_obj_set_width(name, LV_PCT(100));
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    addText(col, detailText(s).c_str(), &font_ui_20, theme::kTextDim);
    // Always there, shown by markFavourites()
    lv_obj_t *star = addText(row, SYM_STAR, &font_ui_22, theme::accent());
    lv_obj_set_hidden(star, !stations::isFavourite(s.url));
    shownRows.push_back(row);
}

// The playing station in the accent, in the list as it is: building it again
// for a tap on another station would scroll it back to the top
void markCurrent() {
    for (size_t i = 0; i < shownRows.size() && i < shown.size(); i++) {
        const bool current = isCurrent(shown[i]);
        lv_obj_t *row = shownRows[i];
        if (rowsArePresets) {
            lv_obj_set_style_border_color(row, lv_color_hex(current ? theme::accent() : theme::kSurface), 0);
            lv_obj_set_style_text_color(lv_obj_get_child(row, 0), lv_color_hex(current ? theme::accent() : theme::kTextDim), 0);
        } else {
            lv_obj_t *name = lv_obj_get_child(lv_obj_get_child(row, 1), 0);
            lv_obj_set_style_text_color(name, lv_color_hex(current ? theme::accent() : theme::kText), 0);
        }
    }
}

// The star on starred stations, also in place: holding one stars it
void markFavourites() {
    if (rowsArePresets) return;  // Favourites itself is built again
    for (size_t i = 0; i < shownRows.size() && i < shown.size(); i++) {
        lv_obj_set_hidden(lv_obj_get_child(shownRows[i], 2), !stations::isFavourite(shown[i].url));
    }
}

// One line, cut with dots
void oneLine(lv_obj_t *label) {
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_max_lines(label, 1);
}

// One per row, so names have room; two and a bit show above the mini
// player, so it's clear the list scrolls
void presets() {
    lv_obj_t *list = addColumn(stList, 10);
    constexpr int32_t kRowHeight = 98;
    for (size_t i = 0; i < shown.size(); i++) {
        const bool current = isCurrent(shown[i]);
        lv_obj_t *row = addRow(list, 16);
        lv_obj_set_height(row, kRowHeight);
        lv_obj_set_style_radius(row, 20, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(theme::kSurface), 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(theme::kSurfaceHi), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, 2, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(current ? theme::accent() : theme::kSurface), 0);
        lv_obj_set_style_pad_hor(row, 18, 0);
        lv_obj_set_clickable(row, true);
        lv_obj_add_event_cb(row, onStationTap, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(row, onStationHold, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)i);

        // The preset number, the station's badge, then the name and what it streams
        lv_obj_t *number = addText(row, String(i + 1).c_str(), &font_ui_30, current ? theme::accent() : theme::kTextDim);
        lv_obj_set_width(number, 36);
        lv_obj_set_style_text_align(number, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t *b = badge(row, shown[i].name, 52);
        lv_obj_set_style_text_font(lv_obj_get_child(b, 0), &font_ui_22, 0);
        lv_obj_t *text = addColumn(row, 2);
        lv_obj_set_width(text, 0);
        lv_obj_set_flex_grow(text, 1);
        oneLine(addText(text, shown[i].name.c_str(), &font_ui_30, theme::kText));
        oneLine(addText(text, detailText(shown[i]).c_str(), &font_ui_22, theme::kTextDim));
        shownRows.push_back(row);
    }
    rowsArePresets = true;

    // "Add": to Popular, where stations get starred. LVGL has no dashed borders.
    lv_obj_t *add = lv_obj_create(list);
    lv_obj_remove_style_all(add);
    lv_obj_set_size(add, LV_PCT(100), 66);
    lv_obj_set_style_radius(add, 20, 0);
    lv_obj_set_style_border_width(add, 2, 0);
    lv_obj_set_style_border_color(add, lv_color_hex(theme::kSurfaceHi), 0);
    lv_obj_set_clickable(add, true);
    lv_obj_add_event_cb(add, [](lv_event_t *) { selectTab(Tab::Popular); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *l = addText(add, LV_SYMBOL_PLUS "  Add", &font_ui_22, theme::kTextDim);
    lv_obj_center(l);
}

void openSearch(lv_event_t *e) {
    openTextInput(lv_obj_get_screen(lv_event_get_target_obj(e)), "Search stations", false,
                  [](const char *q) {
                      if (!*q) return;
                      lastQuery = q;
                      stations::search(q);
                      resultsFor = Tab::Search;
                      rebuildStations();
                  });
}

const char *const kWifiConnecting = "Connecting to Wi-Fi…";
const char *const kWifiSetUp = "Connect to Wi-Fi in Settings to find stations.";

// Why the station directory can't be reached, nullptr once online. With a
// network saved, Wi-Fi keeps trying to get back on it.
const char *offlineText() {
    switch (wifi::status()) {
        case wifi::Status::Connected: return nullptr;
        case wifi::Status::Connecting:
        case wifi::Status::Disconnected: return kWifiConnecting;
        case wifi::Status::Off: return "Wi-Fi is off: swipe down from the top to turn it on.";
        default: return kWifiSetUp;
    }
}

void directoryResults() {
    if (const char *why = offlineText()) {
        message(stList, why);
        return;
    }
    if (resultsFor != stTab) return;
    shownState = stations::results(shown);
    switch (shownState) {
        case stations::State::Busy:
            message(stList, "Loading…");
            break;
        case stations::State::Failed:
            message(stList, "Couldn't reach the station directory. Try again in a moment.");
            break;
        case stations::State::Done: {
            if (shown.empty()) {
                message(stList, "No stations found.");
                break;
            }
            String title;
            if (stTab == Tab::Popular) {
                const String country = stations::countryName();
                title = country.isEmpty() ? "MOST LISTENED" : "MOST LISTENED IN " + country;
            } else {
                title = "RESULTS FOR “" + lastQuery + "”";
            }
            title.toUpperCase();
            addSection(stList, title.c_str());
            for (size_t i = 0; i < shown.size(); i++) stationRow(i);
            hint();
            break;
        }
        default:
            break;
    }
}

void rebuildStations() {
    stShownOffline = offlineText();
    lv_obj_clean(stList);
    shown.clear();
    shownRows.clear();
    rowsArePresets = false;
    switch (stTab) {
        case Tab::Favourites:
            shown = stations::favourites();
            if (shown.empty()) message(stList, "No favourites yet. Hold a station under Popular or Search to add it.");
            presets();
            break;
        case Tab::Recent:
            shown = stations::recent();
            if (shown.empty()) {
                message(stList, "Stations you play show up here.");
            } else {
                addSection(stList, "PLAYED LATELY");
                for (size_t i = 0; i < shown.size(); i++) stationRow(i);
                hint();
            }
            break;
        case Tab::Search: {
            lv_obj_t *card = makeTappable(addCard(stList, "Search for a station…", false), openSearch);
            addValue(card, SYM_SEARCH);
            directoryResults();
            break;
        }
        case Tab::Popular:
            directoryResults();
            break;
    }
}

void selectTab(Tab t) {
    stTab = t;
    for (int i = 0; i < 4; i++) {
        const bool on = i == static_cast<int>(t);
        lv_obj_set_style_bg_color(stTabs[i], lv_color_hex(on ? theme::accent() : theme::kSurfaceHi), 0);
        lv_obj_set_style_text_color(stTabs[i], lv_color_hex(on ? theme::kBg : theme::kText), 0);
    }
    if (t == Tab::Popular && resultsFor != Tab::Popular && wifi::status() == wifi::Status::Connected) {
        stations::loadPopular();
        resultsFor = Tab::Popular;
    }
    rebuildStations();
    lv_obj_scroll_to_y(stList, 0, LV_ANIM_OFF);
}

void updateMiniPlayer() {
    const player::Status st = player::status();
    stations::Station cur;
    const bool have = currentStation(cur);
    const bool active = radio::active();
    setTextIfChanged(miniName, have ? cur.name.c_str() : "Nothing playing");
    setTextIfChanged(miniTitle, active ? (st.title[0] ? st.title : "Playing") : "Stopped");
    setTextIfChanged(miniPlayIcon, active ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY);
}

void miniPlayer(lv_obj_t *footer) {
    lv_obj_t *mini = lv_obj_create(footer);
    lv_obj_remove_style_all(mini);
    lv_obj_set_size(mini, LV_PCT(100), 66);
    lv_obj_set_style_radius(mini, 32, 0);
    lv_obj_set_style_bg_color(mini, lv_color_hex(theme::kSurfaceHi), 0);
    lv_obj_set_style_bg_opa(mini, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(mini, 8, 0);
    lv_obj_set_style_pad_right(mini, 18, 0);
    lv_obj_set_flex_flow(mini, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mini, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(mini, 14, 0);
    lv_obj_set_scrollable(mini, false);
    lv_obj_set_clickable(mini, true);
    lv_obj_add_event_cb(mini, [](lv_event_t *e) { closeSubpage(lv_event_get_target_obj(e)); },
                        LV_EVENT_CLICKED, nullptr);

    lv_obj_t *play = addRoundButton(mini, 50, LV_SYMBOL_PLAY, true, [](lv_event_t *) { radio::togglePlay(); });
    miniPlayIcon = lv_obj_get_child(play, 0);

    lv_obj_t *col = addColumn(mini, 0);
    lv_obj_set_width(col, 0);
    lv_obj_set_flex_grow(col, 1);
    miniName = addText(col, "", &font_ui_22, theme::kText);
    lv_obj_set_width(miniName, LV_PCT(100));
    lv_label_set_long_mode(miniName, LV_LABEL_LONG_DOT);
    miniTitle = addText(col, "", &font_ui_20, theme::accent());
    lv_obj_set_width(miniTitle, LV_PCT(100));
    lv_label_set_long_mode(miniTitle, LV_LABEL_LONG_DOT);
    addText(mini, LV_SYMBOL_RIGHT, &font_ui_22, theme::kTextDim);
}

void tickStations(lv_timer_t *) {
    if (player::revision() != stShownPlayer) {
        stShownPlayer = player::revision();
        updateMiniPlayer();
        markCurrent();
    }
    // Wi-Fi coming up while the list says it's not: Popular starts loading,
    // Search can be used. Going away leaves results there, a short drop
    // shouldn't empty them.
    if (stShownOffline && offlineText() != stShownOffline && (stTab == Tab::Popular || stTab == Tab::Search)) {
        if (stTab == Tab::Popular && resultsFor != Tab::Popular && !offlineText()) {
            stations::loadPopular();
            resultsFor = Tab::Popular;
        }
        rebuildStations();
        return;
    }
    if (stations::revision() == stShownStations) return;
    stShownStations = stations::revision();
    // A lookup finishing rebuilds the list; starring a station in a results
    // list must not (it would jump to the top), except on Favourites
    std::vector<stations::Station> ignored;
    const bool lookupChanged = stations::results(ignored) != shownState;
    if (stTab == Tab::Favourites || lookupChanged) {
        rebuildStations();
    } else {
        markFavourites();
    }
}

void openStations(lv_obj_t *screen) {
    lv_obj_t *body = openSubpage(screen, "Stations", [] { npShownPlayer = UINT32_MAX; }, "INTERNET RADIO");
    lv_obj_t *tabs = addPinnedTop(body, 56);
    lv_obj_set_flex_flow(tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tabs, 8, 0);
    const char *const labels[] = {SYM_STAR, "Popular", "Recent", SYM_SEARCH};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_button_create(tabs);
        lv_obj_set_height(btn, 50);
        if (i == 0 || i == 3) {
            lv_obj_set_width(btn, 74);
        } else {
            lv_obj_set_flex_grow(btn, 1);
        }
        lv_obj_set_style_radius(btn, 24, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_t *l = lv_label_create(btn);
        lv_label_set_text(l, labels[i]);
        lv_obj_center(l);
        lv_obj_add_event_cb(
            btn, [](lv_event_t *e) { selectTab(static_cast<Tab>((intptr_t)lv_event_get_user_data(e))); },
            LV_EVENT_CLICKED, (void *)(intptr_t)i);
        stTabs[i] = btn;
    }
    miniPlayer(addFooter(body, 64));

    stList = lv_obj_create(body);
    lv_obj_remove_style_all(stList);
    lv_obj_set_size(stList, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(stList, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(stList, 10, 0);

    stShownStations = stShownPlayer = UINT32_MAX;
    resultsFor = Tab::Favourites;
    shownState = stations::State::Idle;
    selectTab(stations::favourites().empty() ? Tab::Popular : Tab::Favourites);
    updateMiniPlayer();
    every(stList, 300, tickStations);
}

// ----- Recently heard -----

lv_obj_t *songList;
uint32_t shownSongs = UINT32_MAX;

void rebuildSongs() {
    lv_obj_clean(songList);
    const auto songs = radio::songs();
    if (songs.empty()) {
        message(songList, "No songs yet. Stations that send song titles show up here while you listen.");
        return;
    }
    const bool live = radio::active();
    for (size_t i = 0; i < songs.size(); i++) {
        const radio::Song &s = songs[i];
        const bool playing = i == 0 && live;
        char when[16];
        if (playing) {
            snprintf(when, sizeof(when), "Now");
        } else {
            struct tm t;
            localtime_r(&s.at, &t);
            formatTime(when, sizeof(when), t);
        }
        const String detail = String(when) + " · " + s.station;
        lv_obj_t *card = addTitleCard(songList, s.title.c_str(), &font_ui_22, detail.c_str());
        lv_obj_t *title = lv_obj_get_child(card, 0);
        lv_obj_set_width(title, LV_PCT(100));
        lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
        if (playing) lv_obj_set_style_text_color(title, lv_color_hex(theme::accent()), 0);
    }
    message(songList, "Songs since the radio was switched on.");
}

void openSongs(lv_obj_t *screen) {
    lv_obj_t *body = openSubpage(screen, "Recently heard", nullptr, "INTERNET RADIO");
    songList = addColumn(body, 10);
    shownSongs = radio::songsRevision();
    rebuildSongs();
    every(songList, 1000, [](lv_timer_t *) {
        if (radio::songsRevision() == shownSongs) return;
        shownSongs = radio::songsRevision();
        rebuildSongs();
    });
}

}  // namespace

lv_obj_t *createRadio() {
    lv_obj_t *body = createAppScreen("Internet Radio");
    buildNowPlaying(body);
    every(body, 300, [](lv_timer_t *) { tickNowPlaying(); });
    return lv_obj_get_screen(body);
}

}  // namespace ui::screens
