// Control centre (design/src/screens/ControlCenter.tsx): a screen of its own
// that slides down over what's showing (ui.cpp opens and closes it).
// - Four toggles, 2 x 2: Wi-Fi, the radio, the upload server and the screen lock
// - Brightness and volume
// Built each time it opens, deleted when it closes.

#include <Arduino.h>

#include "app/radio.h"
#include "app/settings.h"
#include "audio/player.h"
#include "hal/display.h"
#include "net/upload_server.h"
#include "net/wifi.h"
#include "screens.h"
#include "theme.h"

namespace ui::screens {

void setWifiEnabled(bool on) {
    if (!on) {
        if (upload_server::running()) upload_server::stop();
        if (radio::active()) player::stop();  // a station; recordings and the card play on
    }
    wifi::setEnabled(on);
}

namespace {

constexpr uint32_t kBg = 0x0D0D0F;
constexpr int32_t kCircle = 96;
constexpr int32_t kSliderHeight = 70;

enum Toggle { kWifi, kRadio, kUpload, kLock, kToggles };

struct ToggleView {
    lv_obj_t *circle, *icon, *state;
    int shown = -1;  // on, off or disabled as last drawn
};
ToggleView toggles[kToggles];
lv_obj_t *volumeSlider;
lv_obj_t *rotateButton;  // after the brightness bar
int shownRotate = -1;
bool syncingVolume = false;  // tick() showing the buttons' change, not a drag

void onToggle(lv_event_t *e) {
    switch ((intptr_t)lv_event_get_user_data(e)) {
        case kWifi:
            setWifiEnabled(!wifi::enabled());
            break;
        case kRadio:
            radio::togglePlay();
            break;
        case kUpload:
            if (upload_server::running()) {
                upload_server::stop();
            } else {
                upload_server::start();
            }
            break;
        case kLock:
            settings::get().screenLock = !settings::get().screenLock;
            settings::changed();
            break;
    }
}

// The circle with its name and state beside it
void addToggle(lv_obj_t *grid, Toggle which, const char *symbol, const char *name) {
    lv_obj_t *cell = addRow(grid, 10);
    lv_obj_set_width(cell, LV_PCT(50));
    ToggleView &t = toggles[which];
    t.circle = addRoundButton(cell, kCircle, symbol, false, onToggle, (void *)(intptr_t)which);
    t.icon = lv_obj_get_child(t.circle, 0);
    lv_obj_set_style_text_font(t.icon, &font_ui_50, 0);
    lv_obj_t *texts = addColumn(cell, 2);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_width(texts, 0);  // what's left of the cell
    addText(texts, name, &font_ui_22, theme::kText);
    t.state = addText(texts, "", &font_ui_20, theme::kTextDim);
    lv_obj_set_width(t.state, LV_PCT(100));
    lv_label_set_long_mode(t.state, LV_LABEL_LONG_DOT);
    // Up to two lines, for a station's name
    lv_obj_set_style_max_height(t.state, 2 * lv_font_get_line_height(&font_ui_20), 0);
    t.shown = -1;
}

void showToggle(Toggle which, bool on, bool disabled, const char *state) {
    ToggleView &t = toggles[which];
    setTextIfChanged(t.state, state);
    const int look = disabled ? 2 : on;
    if (look == t.shown) return;  // a style setter redraws even when nothing changes
    t.shown = look;
    lv_obj_set_style_bg_color(t.circle, lv_color_hex(on ? theme::accent() : theme::kSurfaceHi), 0);
    lv_obj_set_style_text_color(t.icon, lv_color_hex(on ? theme::kBg : theme::kText), 0);
    lv_obj_set_clickable(t.circle, !disabled);
    lv_obj_set_style_opa(lv_obj_get_parent(t.circle), disabled ? LV_OPA_40 : LV_OPA_COVER, 0);
}

void tick(lv_timer_t *) {
    const wifi::Status ws = wifi::status();
    const bool wifiOn = wifi::enabled();
    const bool online = ws == wifi::Status::Connected;
    const String ssid = wifi::ssid();
    showToggle(kWifi, wifiOn, false,
               !wifiOn                              ? "Off"
               : online                             ? ssid.c_str()
               : ws == wifi::Status::Connecting     ? "Joining…"  // "Connecting…" breaks mid-word
               : ws == wifi::Status::NotConfigured  ? "Not set up"
                                                    : "Not connected");

    const bool playing = radio::active();
    const player::Status ps = player::status();
    showToggle(kRadio, playing, !wifiOn, !wifiOn ? "No Wi-Fi" : playing ? (ps.name[0] ? ps.name : "On") : "Off");

    const bool uploading = upload_server::running();
    showToggle(kUpload, uploading, !online && !uploading,
               uploading ? "On" : !wifiOn ? "No Wi-Fi" : !online ? "Not connected" : "Off");

    const bool lock = settings::get().screenLock;
    showToggle(kLock, lock, false, lock ? "On" : "Off");

    const bool rotate = settings::get().autoRotate;
    if ((int)rotate != shownRotate) {
        shownRotate = rotate;
        lv_obj_set_style_bg_color(rotateButton, lv_color_hex(rotate ? theme::accent() : theme::kSurfaceHi), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(rotateButton, 0), lv_color_hex(rotate ? theme::kBg : theme::kText), 0);
    }

    // The side buttons change the volume meanwhile
    if (lv_slider_get_value(volumeSlider) != radio::volume() && !lv_obj_has_state(volumeSlider, LV_STATE_PRESSED)) {
        lv_slider_set_value(volumeSlider, radio::volume(), LV_ANIM_OFF);
        syncingVolume = true;  // the bar's own figure follows; the volume is already set
        lv_obj_send_event(volumeSlider, LV_EVENT_VALUE_CHANGED, nullptr);
        syncingVolume = false;
    }
}

}  // namespace

lv_obj_t *createControlCenter() {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(kBg), 0);
    lv_obj_set_scrollable(scr, false);
    lv_obj_set_style_pad_top(scr, 20, 0);
    lv_obj_set_style_pad_hor(scr, 12, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr, 16, 0);
    lv_obj_set_style_text_font(scr, &font_ui_22, 0);
    lv_obj_set_style_text_color(scr, lv_color_hex(theme::kText), 0);

    lv_obj_t *grid = addRow(scr, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(grid, 16, 0);
    lv_obj_set_style_margin_bottom(grid, 6, 0);
    addToggle(grid, kWifi, LV_SYMBOL_WIFI, "Wi-Fi");
    addToggle(grid, kRadio, LV_SYMBOL_AUDIO, "Radio");
    addToggle(grid, kUpload, LV_SYMBOL_UPLOAD, "Upload");
    addToggle(grid, kLock, SYM_LOCK, "Lock");

    // Brightness, and the auto-rotate button after it, as tall as the bar
    lv_obj_t *brightRow = addRow(scr, 12);
    auto toPercent = [](uint8_t level) { return (level * 100 + 127) / 255; };
    lv_obj_t *bright = addBigSlider(brightRow, 2, 100, toPercent(settings::get().brightness), SYM_SUN, [](lv_event_t *e) {
        const int percent = lv_slider_get_value(lv_event_get_target_obj(e));
        settings::get().brightness = (percent * 255 + 50) / 100;
        display::setBrightness(settings::get().brightness);
        settings::changed();
    });
    lv_obj_set_height(bright, kSliderHeight);
    lv_obj_set_width(bright, 0);
    lv_obj_set_flex_grow(bright, 1);
    rotateButton = addRoundButton(brightRow, kSliderHeight, LV_SYMBOL_REFRESH, false, [](lv_event_t *) {
        settings::get().autoRotate = !settings::get().autoRotate;
        settings::changed();
        tick(nullptr);
    });
    lv_obj_set_style_text_font(lv_obj_get_child(rotateButton, 0), &font_ui_30, 0);
    shownRotate = -1;
    volumeSlider = addBigSlider(scr, 0, 100, radio::volume(), LV_SYMBOL_VOLUME_MAX, [](lv_event_t *e) {
        if (!syncingVolume) radio::setVolume(lv_slider_get_value(lv_event_get_target_obj(e)));
    });
    lv_obj_set_height(volumeSlider, kSliderHeight);
    for (lv_obj_t *s : {bright, volumeSlider}) {
        lv_obj_set_style_radius(s, kSliderHeight / 2, LV_PART_MAIN);
        lv_obj_set_style_radius(s, kSliderHeight / 2, LV_PART_INDICATOR);
    }

    // The grab bar, in the middle of the ~74 px left under the volume: room to
    // start a swipe up without touching the bar above
    lv_obj_t *handle = lv_obj_create(scr);
    lv_obj_remove_style_all(handle);
    lv_obj_set_floating(handle, true);  // outside the column
    lv_obj_set_size(handle, 56, 5);
    lv_obj_align(handle, LV_ALIGN_BOTTOM_MID, 0, -34);
    lv_obj_set_style_radius(handle, 3, 0);
    lv_obj_set_style_bg_color(handle, lv_color_hex(theme::kSurfaceHi), 0);
    lv_obj_set_style_bg_opa(handle, LV_OPA_COVER, 0);
    lv_obj_set_clickable(handle, false);

    tick(nullptr);
    every(scr, 300, tick);
    return scr;
}

}  // namespace ui::screens
