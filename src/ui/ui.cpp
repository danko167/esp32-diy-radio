#include "ui.h"

#include <Arduino.h>
#include <lvgl.h>

#include "app/alarms.h"
#include "app/radio.h"
#include "app/settings.h"
#include "audio/player.h"
#include "audio/recorder.h"
#include "hal/buttons.h"
#include "hal/display.h"
#include "hal/imu.h"
#include "hal/pins.h"
#include "hal/power.h"
#include "hal/rtc.h"
#include "net/upload_server.h"
#include "screens.h"
#include "theme.h"

namespace ui {
namespace {

constexpr uint32_t kSlideMs = 260;
constexpr uint32_t kAmbientDipMs = 400;
constexpr uint32_t kAmbientRiseMs = 900;
constexpr uint32_t kWakeMs = 250;
// Waking mirrors going to the clock screen, quicker: dip to black, swap, rise
constexpr uint32_t kWakeDipMs = 150;
constexpr uint32_t kWakeRiseMs = 450;

lv_obj_t *home = nullptr;
lv_obj_t *ambient = nullptr;
lv_obj_t *appScreen = nullptr;  // open app, nullptr while on home
App currentApp = App::Settings;  // which app appScreen is
bool inAmbient = false;
bool waking = false;  // wake()'s dip to black, before the swap (still inAmbient)
int brightnessAnimVar;  // just a unique address to identify the animation
lv_obj_t *updatePanel = nullptr;  // showUpdateProgress()'s, while an update runs
lv_obj_t *emptyPanel = nullptr;   // "Battery empty", in the seconds before switching off

// The control centre: a swipe down from the top edge opens it over what's
// showing (home or an app), a swipe up or the middle button closes it
constexpr int32_t kEdge = 36;  // px from the top a swipe has to start in
lv_obj_t *ccScreen = nullptr;
lv_obj_t *ccUnder = nullptr;  // the screen it opened over, shown again when it closes
lv_point_t pressAt;           // where the finger went down

// The clock screen alone redraws once a minute, so the CPU drops to 80 MHz
// there while nothing plays, to save battery. Peripheral clocks stay the
// same (they run from the 80 MHz bus), and Wi-Fi stays connected.
constexpr uint32_t kFullMhz = 240;
constexpr uint32_t kIdleMhz = 80;

void setCpuMhz(uint32_t mhz) {
    if (getCpuFrequencyMhz() != mhz) setCpuFrequencyMhz(mhz);
}

// Slides don't move live screens: redrawing every widget each frame runs at
// ~12 fps here. Instead both screens are rendered once into images, the images
// slide, and the real screen takes over at the end.
lv_obj_t *transition = nullptr;  // screen holding the two images while sliding
lv_obj_t *slideTarget = nullptr;
lv_obj_t *slideDelete = nullptr;
lv_draw_buf_t *appShot = nullptr;
lv_draw_buf_t *changeShots[2] = {};  // slideChange()'s before and after
lv_draw_buf_t *homeShot = nullptr;  // kept between slides, home rarely changes
uint32_t homeShotVersion = 0;

lv_draw_buf_t *takeSnapshot(lv_obj_t *scr) {
#ifdef PERF_LOG
    const uint32_t start = micros();
#endif
    lv_obj_update_layout(scr);
    lv_draw_buf_t *shot = lv_snapshot_take(scr, LV_COLOR_FORMAT_RGB565);
#ifdef PERF_LOG
    Serial.printf("snapshot: %lu us\n", micros() - start);
#endif
    return shot;
}

lv_draw_buf_t *homeSnapshot() {
    if (!homeShot || homeShotVersion != screens::homeVersion()) {
        if (homeShot) lv_draw_buf_destroy(homeShot);
        homeShot = takeSnapshot(home);
        homeShotVersion = screens::homeVersion();
    }
    return homeShot;
}

// A finger still down when the screens change touches nothing on the new one
// (a screen load forgets lv_indev_wait_release()): the rest of that touch is ignored
void ignoreRestOfTouch() {
    lv_indev_t *indev = lv_indev_get_next(nullptr);
    if (indev && lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED) lv_indev_wait_release(indev);
}

void finishSlide(lv_anim_t *) {
    lv_screen_load(slideTarget);
    ignoreRestOfTouch();
    if (transition) lv_obj_delete(transition);  // its images go before the buffers they show
    transition = nullptr;
    if (slideDelete) lv_obj_delete(slideDelete);
    slideDelete = nullptr;
    if (appShot) lv_draw_buf_destroy(appShot);
    appShot = nullptr;
    for (lv_draw_buf_t *&shot : changeShots) {
        if (shot) lv_draw_buf_destroy(shot);
        shot = nullptr;
    }
}

// `left` and `right` sit side by side; the pair moves one screen width.
// Ends on `target`, then deletes `toDelete` (if any).
void slide(lv_draw_buf_t *left, lv_draw_buf_t *right, bool moveLeft, lv_obj_t *target,
           lv_obj_t *toDelete) {
    slideTarget = target;
    slideDelete = toDelete;
    if (!left || !right) {  // out of memory: just switch
        finishSlide(nullptr);
        return;
    }

    transition = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(transition, lv_color_black(), 0);
    lv_obj_set_scrollable(transition, false);
    lv_obj_t *strip = lv_obj_create(transition);
    lv_obj_remove_style_all(strip);
    lv_obj_set_size(strip, LCD_WIDTH * 2, LCD_HEIGHT);
    lv_obj_set_scrollable(strip, false);
    lv_obj_t *img = lv_image_create(strip);
    lv_image_set_src(img, left);
    img = lv_image_create(strip);
    lv_image_set_src(img, right);
    lv_obj_set_x(img, LCD_WIDTH);

    const int32_t from = moveLeft ? 0 : -LCD_WIDTH;
    lv_obj_set_x(strip, from);
    lv_screen_load(transition);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, strip);
    lv_anim_set_values(&a, from, moveLeft ? -LCD_WIDTH : 0);
    lv_anim_set_duration(&a, kSlideMs);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, [](void *obj, int32_t x) { lv_obj_set_x(static_cast<lv_obj_t *>(obj), x); });
    lv_anim_set_completed_cb(&a, finishSlide);
    lv_anim_start(&a);
}

// The control centre's slide: `panel` comes down over `under` (or goes back up)
void slideVertical(lv_draw_buf_t *under, lv_draw_buf_t *panel, bool down, lv_obj_t *target,
                   lv_obj_t *toDelete) {
    slideTarget = target;
    slideDelete = toDelete;
    if (!under || !panel) {  // out of memory: just switch
        finishSlide(nullptr);
        return;
    }
    transition = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(transition, lv_color_black(), 0);
    lv_obj_set_scrollable(transition, false);
    lv_image_set_src(lv_image_create(transition), under);
    lv_obj_t *top = lv_image_create(transition);
    lv_image_set_src(top, panel);
    lv_obj_set_y(top, down ? -LCD_HEIGHT : 0);
    lv_screen_load(transition);
    ignoreRestOfTouch();

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, top);
    lv_anim_set_values(&a, down ? -LCD_HEIGHT : 0, down ? 0 : -LCD_HEIGHT);
    lv_anim_set_duration(&a, kSlideMs);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, [](void *obj, int32_t y) { lv_obj_set_y(static_cast<lv_obj_t *>(obj), y); });
    lv_anim_set_completed_cb(&a, finishSlide);
    lv_anim_start(&a);
}

// Fades use the panel's own brightness, never a redraw: a full-screen redraw
// takes longer than one panel refresh, so fading pixels shows as moving bands.
void fadeBrightness(uint8_t to, uint32_t ms, lv_anim_completed_cb_t done = nullptr) {
    lv_anim_delete(&brightnessAnimVar, nullptr);  // a cancelled fade never calls `done`
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &brightnessAnimVar);
    lv_anim_set_values(&a, display::brightness(), to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, [](void *, int32_t v) { display::setBrightness(v); });
    lv_anim_set_completed_cb(&a, done);
    lv_anim_start(&a);
}

// Dip to black, swap to the ambient screen while dark, rise to the ambient level
void enterAmbient() {
    inAmbient = true;
    fadeBrightness(0, kAmbientDipMs, [](lv_anim_t *) {
        const auto &s = settings::get();
        screens::showAmbient();
        lv_screen_load(ambient);
        if (ccScreen) {  // gone while dark; waking shows what it was over
            lv_obj_delete(ccScreen);
            ccScreen = nullptr;
        }
        if (s.ambientMode != settings::AmbientMode::ScreenOff) {
            fadeBrightness(s.ambientBrightness, kAmbientRiseMs);
        }
    });
}

lv_obj_t *ringPanel = nullptr;
lv_obj_t *ringClock = nullptr;

lv_obj_t *ringButton(lv_obj_t *parent, const char *label, bool primary, int32_t height, lv_event_cb_t cb) {
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, LV_PCT(100), height);
    lv_obj_set_style_radius(btn, 32, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(primary ? theme::accent() : theme::kSurfaceHi), 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(primary ? theme::kBg : theme::kText), 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, label);
    lv_obj_center(l);
    return btn;
}

// Full-screen ringing alarm, on the top layer so it covers whatever is open.
// A huge Snooze for the half-asleep, Stop below it.
void showRinging(const alarms::Alarm &a) {
    setCpuMhz(kFullMhz);
    if (inAmbient) {  // straight out of the ambient clock
        inAmbient = false;
        waking = false;  // its fade is cancelled below
        lv_screen_load(appScreen ? appScreen : home);
        screens::hideAmbient();
    }
    lv_anim_delete(&brightnessAnimVar, nullptr);
    display::setBrightness(settings::get().brightness);

    ringPanel = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(ringPanel);
    lv_obj_set_size(ringPanel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ringPanel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ringPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_top(ringPanel, 24, 0);
    lv_obj_set_style_pad_hor(ringPanel, 24, 0);
    lv_obj_set_style_pad_bottom(ringPanel, 16, 0);
    lv_obj_set_clickable(ringPanel, true);  // nothing underneath reacts
    lv_obj_set_scrollable(ringPanel, false);
    lv_obj_set_flex_flow(ringPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ringPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    String title = String(LV_SYMBOL_BELL "  ") + (a.label.isEmpty() ? "Alarm" : a.label.c_str());
    lv_obj_t *label = lv_label_create(ringPanel);
    lv_label_set_text(label, title.c_str());
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(label, LV_PCT(100), 0);
    lv_obj_set_style_text_font(label, &font_ui_30, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(theme::accent()), 0);
    ringClock = lv_label_create(ringPanel);
    lv_obj_set_style_text_font(ringClock, &font_clock_140, 0);
    lv_obj_set_style_pad_top(ringClock, 4, 0);
    lv_obj_set_style_text_color(ringClock, lv_color_hex(theme::kText), 0);
    lv_label_set_text(ringClock, "");
    String sound = a.soundName + (a.gentle ? " · fading in" : "");
    lv_obj_t *detail = lv_label_create(ringPanel);
    lv_label_set_text(detail, sound.c_str());
    lv_obj_set_style_text_font(detail, &font_ui_20, 0);
    lv_obj_set_style_text_color(detail, lv_color_hex(theme::kTextDim), 0);
    lv_obj_set_style_pad_bottom(detail, 20, 0);

    char snooze[24];
    snprintf(snooze, sizeof(snooze), "Snooze %u min", a.snoozeMinutes);
    lv_obj_t *snoozeButton = ringButton(ringPanel, snooze, false, 106, [](lv_event_t *) { alarms::snooze(); });
    lv_obj_set_style_text_font(snoozeButton, &font_ui_30, 0);
    lv_obj_t *stop = ringButton(ringPanel, "Stop", true, 66, [](lv_event_t *) { alarms::dismiss(); });
    lv_obj_set_style_margin_top(stop, 10, 0);

    lv_obj_t *spacer = lv_obj_create(ringPanel);
    lv_obj_remove_style_all(spacer);
    lv_obj_set_flex_grow(spacer, 1);
    lv_obj_t *hint = lv_label_create(ringPanel);
    lv_label_set_text(hint, "Side buttons snooze · middle button stops");
    lv_obj_set_style_text_font(hint, &font_ui_20, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(theme::kTextDim), 0);
}

void checkRinging(lv_timer_t *) {
    if (emptyPanel) return;  // switching off: nothing comes over it
    static uint32_t shownId = 0;
    alarms::Alarm a;
    const bool ringing = alarms::ringing(&a);
    if (ringPanel && (!ringing || a.id != shownId)) {  // stopped, or a later alarm took over
        lv_obj_delete(ringPanel);
        ringPanel = ringClock = nullptr;
        lv_display_trigger_activity(lv_display_get_default());  // stay awake a moment after stopping it
    }
    if (ringing && !ringPanel) {
        showRinging(a);
        shownId = a.id;
    }
    if (ringClock) {
        struct tm t;
        char text[16];
        if (screens::localNow(t)) {
            screens::formatTime(text, sizeof(text), t);
            screens::setTextIfChanged(ringClock, text);
        }
        lv_display_trigger_activity(lv_display_get_default());  // no ambient screen while ringing
    }
}

void checkInactivity(lv_timer_t *) {
    const auto &s = settings::get();
    const uint32_t idleMs = lv_display_get_inactive_time(lv_display_get_default());
    if (inAmbient || transition || ringPanel || emptyPanel) return;

    // Keep the home snapshot fresh while nobody is touching, so opening an
    // app doesn't have to render home first
    if (!appScreen && idleMs > 1500) homeSnapshot();

    if (s.ambientTimeoutS != 0 && idleMs >= s.ambientTimeoutS * 1000u) enterAmbient();
}

// Full speed again comes first in wake() and showRinging(); this catches the rest,
// such as the radio started from the clock screen
void updateCpuSpeed() {
    const player::State s = player::status().state;
    // The upload server too: at 80 MHz a picture's thumbnails crawled
    const bool idle = inAmbient && !waking && !ringPanel && !transition && !screens::unlockShowing() &&
                      s != player::State::Playing &&
                      s != player::State::Connecting && !upload_server::running() && !recorder::recording();
    setCpuMhz(idle ? kIdleMhz : kFullMhz);
}

void tickClocks(lv_timer_t *) {
    updateCpuSpeed();
    if (inAmbient) {
        screens::tickAmbient();
    } else {
        screens::tickHome();
    }
}

void applyPerfOverlay() {
#if LV_USE_PERF_MONITOR
    lv_display_t *disp = lv_display_get_default();
    if (settings::get().perfOverlay) {
        lv_sysmon_show_performance(disp);
    } else {
        lv_sysmon_hide_performance(disp);
    }
#endif
}

// ----- Buttons -----

constexpr int kVolumeStep = 5;
constexpr uint32_t kVolumeHudMs = 1500;

lv_obj_t *volumeHud = nullptr;
lv_obj_t *volumeBar;
lv_obj_t *volumeLabel;
lv_timer_t *volumeHudTimer;

// A small bar at the top whenever the buttons change the volume
void showVolume(int v) {
    if (!volumeHud) {
        volumeHud = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(volumeHud);
        lv_obj_set_size(volumeHud, 300, 58);
        lv_obj_align(volumeHud, LV_ALIGN_TOP_MID, 0, 12);
        lv_obj_set_style_radius(volumeHud, 28, 0);
        lv_obj_set_style_bg_color(volumeHud, lv_color_hex(theme::kSurfaceHi), 0);
        lv_obj_set_style_bg_opa(volumeHud, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(volumeHud, 20, 0);
        lv_obj_set_flex_flow(volumeHud, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(volumeHud, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(volumeHud, 14, 0);
        lv_obj_set_clickable(volumeHud, false);  // taps go through to what's underneath (the bar's too, below)
        // The top layer is outside any screen, so the theme's text colour
        // doesn't reach it: without this, the icon and % were LVGL's black
        lv_obj_set_style_text_color(volumeHud, lv_color_hex(theme::kText), 0);
        lv_obj_t *icon = lv_label_create(volumeHud);
        lv_label_set_text(icon, LV_SYMBOL_VOLUME_MAX);
        volumeBar = lv_bar_create(volumeHud);
        lv_obj_set_flex_grow(volumeBar, 1);
        lv_obj_set_height(volumeBar, 10);
        lv_obj_set_clickable(volumeBar, false);
        volumeLabel = lv_label_create(volumeHud);
        lv_obj_set_width(volumeLabel, 54);
        lv_obj_set_style_text_align(volumeLabel, LV_TEXT_ALIGN_RIGHT, 0);
        volumeHudTimer = lv_timer_create(
            [](lv_timer_t *t) {
                lv_obj_set_hidden(volumeHud, true);
                lv_timer_pause(t);
            },
            kVolumeHudMs, nullptr);
    }
    lv_bar_set_value(volumeBar, v, LV_ANIM_OFF);
    lv_label_set_text_fmt(volumeLabel, "%d%%", v);
    lv_obj_set_hidden(volumeHud, false);
    lv_obj_move_foreground(volumeHud);  // over the low battery notice, in the same place
    lv_timer_reset(volumeHudTimer);
    lv_timer_resume(volumeHudTimer);
}

void changeVolume(int delta) {
    radio::setVolume(radio::volume() + delta);
    showVolume(radio::volume());
}

// Left and right: volume (held: keeps going). Middle depends on where you are.
// No double presses: tapping volume quickly would trigger them.
void onButton(buttons::Key key, buttons::Gesture g) {
    using buttons::Gesture;
    using buttons::Key;
    if (emptyPanel) return;  // switching off
    lv_display_trigger_activity(lv_display_get_default());  // using the buttons counts as being here

    if (alarms::ringing()) {
        if (g != Gesture::Press && g != Gesture::Long) return;
        if (key == Key::Middle) {
            alarms::dismiss();
        } else {
            alarms::snooze();
        }
        return;
    }

    if (key != Key::Middle) {
        changeVolume((key == Key::Right ? 1 : -1) * kVolumeStep);
        return;
    }
    if (ccScreen && !inAmbient) {  // the control centre: closes it (fading to the clock: wakes)
        if (g == Gesture::Press) closeControlCenter();
        return;
    }

    if (transition || (g != Gesture::Press && g != Gesture::Long)) return;
    const bool press = g == Gesture::Press;
    if (inAmbient) {  // clock: wake, or hold for radio on/off at bedtime
        press ? wake() : radio::togglePlay();
    } else if (!appScreen) {  // home: off to the clock, or radio on/off
        press ? sleep() : radio::togglePlay();
    } else if (currentApp == App::Internet) {
        press ? radio::togglePlay() : back();
    } else {  // other apps: back home, or straight to the clock
        press ? back() : sleep();
    }
}

// ----- Battery -----

constexpr uint32_t kLowNoticeMs = 8000;
constexpr uint32_t kLowAgainMs = 10 * 60 * 1000;  // while it stays low
constexpr uint32_t kEmptyMs = 3000;  // "Battery empty" shows this long before switching off
constexpr uint32_t kEmptyWaitMs = 250;   // then, while a recording is still being closed, looks again this often
constexpr uint32_t kEmptyMaxMs = 8000;   // but switches off by then anyway

lv_obj_t *lowNotice = nullptr;
lv_timer_t *lowNoticeTimer;

// Like the volume bar, wider. Not touchable and not activity, so on the
// clock screen it shows at the clock's dim level and leaves the clock there.
void showLowNotice() {
    if (!lowNotice) {
        lowNotice = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(lowNotice);
        lv_obj_set_size(lowNotice, 400, 58);
        lv_obj_align(lowNotice, LV_ALIGN_TOP_MID, 0, 12);
        lv_obj_set_style_radius(lowNotice, 28, 0);
        lv_obj_set_style_bg_color(lowNotice, lv_color_hex(theme::kSurfaceHi), 0);
        lv_obj_set_style_bg_opa(lowNotice, LV_OPA_COVER, 0);
        lv_obj_set_flex_flow(lowNotice, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(lowNotice, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(lowNotice, 14, 0);
        lv_obj_set_style_text_color(lowNotice, lv_color_hex(theme::kText), 0);  // the top layer has no theme
        lv_obj_set_clickable(lowNotice, false);  // a tap on it still wakes the clock screen
        lv_obj_t *icon = lv_label_create(lowNotice);
        lv_label_set_text(icon, LV_SYMBOL_BATTERY_EMPTY);
        lv_obj_set_style_text_color(icon, lv_color_hex(theme::kRed), 0);
        lv_label_set_text(lv_label_create(lowNotice), "Battery low · plug in");
        lowNoticeTimer = lv_timer_create(
            [](lv_timer_t *t) {
                lv_obj_set_hidden(lowNotice, true);
                lv_timer_pause(t);
            },
            kLowNoticeMs, nullptr);
    }
    lv_obj_set_hidden(lowNotice, false);
    lv_obj_move_foreground(lowNotice);
    lv_timer_reset(lowNoticeTimer);
    lv_timer_resume(lowNoticeTimer);
}

void hideLowNotice() {
    if (!lowNotice || lv_obj_is_hidden(lowNotice)) return;
    lv_obj_set_hidden(lowNotice, true);
    lv_timer_pause(lowNoticeTimer);
}

// Nearly empty: sound off, everything saved, and off before the chip cuts the
// power by itself, maybe in the middle of writing the flash. At whatever
// brightness the screen has, so a dim clock screen stays dim.
void switchOff() {
    Serial.println("power: battery empty, switching off");
    player::stop();
    if (upload_server::running()) upload_server::stop();
    recorder::stop("Stopped: the battery was empty");  // closed in the seconds till the power goes
    settings::saveNow();
    if (rtc::timeValid()) rtc::save();
    hideLowNotice();

    emptyPanel = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(emptyPanel);
    lv_obj_set_size(emptyPanel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(emptyPanel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(emptyPanel, LV_OPA_COVER, 0);
    lv_obj_set_clickable(emptyPanel, true);  // nothing underneath reacts
    lv_obj_set_scrollable(emptyPanel, false);
    lv_obj_set_flex_flow(emptyPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(emptyPanel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(emptyPanel, 16, 0);
    lv_obj_set_style_text_font(emptyPanel, &font_ui_30, 0);
    lv_obj_t *icon = lv_label_create(emptyPanel);
    lv_label_set_text(icon, LV_SYMBOL_BATTERY_EMPTY);
    lv_obj_set_style_text_color(icon, lv_color_hex(theme::kRed), 0);
    lv_obj_t *label = lv_label_create(emptyPanel);
    lv_label_set_text(label, "Battery empty · switching off");
    lv_obj_set_style_text_color(label, lv_color_hex(theme::kText), 0);

    static uint32_t emptyAt;
    emptyAt = millis();
    lv_timer_create(
        [](lv_timer_t *t) {
            // Cutting the power before the WAV file is closed would lose the recording
            if (recorder::recording() && millis() - emptyAt < kEmptyMaxMs) {
                lv_timer_set_period(t, kEmptyWaitMs);
                return;
            }
            lv_timer_delete(t);
            // Plugged in meanwhile: stays on. Pretending (dev console B), shutdown() only says so.
            if (!power::battery().usb) power::shutdown();
            lv_obj_delete(emptyPanel);
            emptyPanel = nullptr;
            lv_display_trigger_activity(lv_display_get_default());
        },
        kEmptyMs, nullptr);
}

// Each change of level once; Low's notice again every so often. Never during
// a firmware update: the chip's own cut-off is further down, the update can finish.
// Auto-rotate (the control centre's button, off by default): the screen
// follows the radio stood on its left or right side, to charge it from the
// side and still see the clock. The motion sensor is on only for this (or the
// Level). The Level always shows the normal way round.
constexpr uint32_t kRotateCheckMs = 200;
constexpr int kRotateSteadyChecks = 5;  // a second on the new side before it turns
constexpr float kRotateG = 0.75f;       // the side down carries most of gravity
constexpr uint32_t kRotateDipMs = 120;
constexpr uint32_t kRotateRiseMs = 250;
bool rotateUsesSensor = false;  // the sensor is on for this, not the Level
display::Rotation rotateSeen = display::Rotation::Normal;
int rotateSeenFor = 0;
display::Rotation rotateTo = display::Rotation::Normal;
uint8_t rotateBack = 0;  // the brightness to rise to again

// Which way the radio stands, from the sensor's "up" in the screen's axes
// (upright: y = 1; on its left side, up points to the screen's right: x = 1).
// Lying flat or in between: as it is now.
display::Rotation standing(float x, float y) {
    if (x > kRotateG) return display::Rotation::Left;
    if (x < -kRotateG) return display::Rotation::Right;
    if (y > kRotateG) return display::Rotation::Normal;
    return display::rotation();
}

void applyRotation() {
    display::setRotation(rotateTo);
    lv_obj_invalidate(lv_screen_active());
    lv_obj_invalidate(lv_layer_top());
}

// Through black like waking, so the turn is a cut, not a redraw sweeping across
void turnScreen(display::Rotation r) {
    rotateTo = r;
    rotateBack = display::brightness();
    if (!rotateBack) return applyRotation();  // the screen is off: nothing to hide
    fadeBrightness(0, kRotateDipMs, [](lv_anim_t *) {
        applyRotation();
        lv_refr_now(nullptr);  // the whole turned picture before it rises
        fadeBrightness(rotateBack, kRotateRiseMs);
    });
}

void checkRotation(lv_timer_t *) {
    // Not over another fade (waking, the clock screen) or a slide: next time
    if (transition || waking || updatePanel || emptyPanel || lv_anim_get(&brightnessAnimVar, nullptr)) return;
    const bool level = appScreen && currentApp == App::Level;
    if (!settings::get().autoRotate || level || !imu::present()) {
        // The Level has the sensor to itself while it's open, and stops it when it closes
        if (rotateUsesSensor && !level) imu::stop();
        rotateUsesSensor = false;
        rotateSeenFor = 0;
        if (display::rotation() != display::Rotation::Normal) turnScreen(display::Rotation::Normal);
        return;
    }
    if (!imu::running()) {
        imu::start();
        rotateUsesSensor = true;
        return;
    }
    float x, y, z;
    if (!imu::read(x, y, z)) return;
    const display::Rotation side = standing(x, y);
    if (side != rotateSeen) {
        rotateSeen = side;
        rotateSeenFor = 0;
    }
    if (side != display::rotation() && ++rotateSeenFor >= kRotateSteadyChecks) {
        rotateSeenFor = 0;
        turnScreen(side);
    }
}

void checkBattery(lv_timer_t *) {
    static power::Level handled = power::Level::Ok;
    static uint32_t lowShownAt = 0;
    if (updatePanel || emptyPanel) return;
    const power::Level level = power::battery().level;
    if (level == power::Level::Critical) {
        if (handled != level) switchOff();
    } else if (level == power::Level::Low) {
        if (ringPanel) return;  // not over a ringing alarm: after it
        if (handled != level || millis() - lowShownAt >= kLowAgainMs) {
            showLowNotice();
            lowShownAt = millis();
        }
    } else {
        hideLowNotice();  // plugged in
    }
    handled = level;
}

}  // namespace

void begin() {
    theme::begin();
    home = screens::createHome();
    ambient = screens::createAmbient();
    lv_screen_load(home);
    applyPerfOverlay();

    // Fade in at boot instead of popping on
    display::setBrightness(0);
    fadeBrightness(settings::get().brightness, 600);

    lv_timer_create(checkInactivity, 500, nullptr);
    lv_timer_create(tickClocks, 250, nullptr);
    lv_timer_create(checkRinging, 250, nullptr);
    lv_timer_create(checkBattery, 1000, nullptr);
    lv_timer_create(checkRotation, kRotateCheckMs, nullptr);
    buttons::begin(onButton, nullptr);  // no double presses anywhere

    // A swipe down from the top edge, anywhere but the clock screen, opens the
    // control centre; a swipe up on it closes it. Watched on the touch input
    // itself, so every screen gets it without its own code.
    lv_indev_add_event_cb(
        lv_indev_get_next(nullptr),
        [](lv_event_t *e) {
            lv_indev_t *indev = lv_indev_active();
            if (!indev) return;
            if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
                lv_indev_get_point(indev, &pressAt);
                return;
            }
            if (lv_event_get_code(e) != LV_EVENT_GESTURE) return;
            const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
            const bool opening = dir == LV_DIR_BOTTOM && !ccScreen && pressAt.y < kEdge;
            const bool closing = dir == LV_DIR_TOP && ccScreen && lv_screen_active() == ccScreen;
            if (!opening && !closing) return;
            if (inAmbient || transition || ringPanel || emptyPanel || updatePanel) return;
            lv_indev_wait_release(indev);  // nothing under the finger reacts to the rest of it
            // Not from inside the touch handling: the screens change
            lv_async_call([](void *open) { open ? openControlCenter() : closeControlCenter(); },
                          opening ? (void *)1 : nullptr);
        },
        LV_EVENT_ALL, nullptr);
}

// What the control centre slides over: home's cached image when it's home
lv_draw_buf_t *underShot() {
    if (ccUnder == home) return homeSnapshot();  // kept, not freed after the slide
    changeShots[0] = takeSnapshot(ccUnder);
    return changeShots[0];
}

void openControlCenter() {
    if (ccScreen || transition || inAmbient || ringPanel || emptyPanel || updatePanel) return;
    ccUnder = lv_screen_active();
    ccScreen = screens::createControlCenter();
    lv_draw_buf_t *under = underShot();
    changeShots[1] = takeSnapshot(ccScreen);
    slideVertical(under, changeShots[1], true, ccScreen, nullptr);
}

void closeControlCenter() {
    // Not while going to the clock screen: that drops it in the dark itself
    if (!ccScreen || transition || inAmbient) return;
    lv_obj_t *cc = ccScreen;
    ccScreen = nullptr;
    lv_draw_buf_t *under = underShot();
    changeShots[1] = takeSnapshot(cc);
    slideVertical(under, changeShots[1], false, ccUnder, cc);
}

bool controlCenterOpen() { return ccScreen != nullptr; }

void open(App app) {
    if (appScreen || inAmbient || transition || ccScreen) return;
    currentApp = app;
    switch (app) {
        case App::Settings:
            appScreen = screens::createSettings();
            break;
        case App::FmRadio:
            appScreen = screens::createPlaceholder(
                "FM Radio", SYM_TOWER,
                "Tuning, station names from RDS and your favourite presets.");
            break;
        case App::Internet:
            appScreen = screens::createRadio();
            break;
        case App::Bluetooth:
            appScreen = screens::createPlaceholder(
                "Bluetooth", LV_SYMBOL_BLUETOOTH, "Play music from your phone.");
            break;
        case App::Weather:
            appScreen = screens::createWeather();
            break;
        case App::Alarm:
            appScreen = screens::createAlarms();
            break;
        case App::Recorder:
            appScreen = screens::createRecorder();
            break;
        case App::Level:
            appScreen = screens::createLevel();
            break;
    }
    appShot = takeSnapshot(appScreen);
    slide(homeSnapshot(), appShot, true, appScreen, nullptr);
}

void back(Exit exit) {
    if (!appScreen || inAmbient || transition || ccScreen) return;
    lv_obj_t *leaving = appScreen;
    appScreen = nullptr;
    appShot = takeSnapshot(leaving);
    if (exit == Exit::ToRight) {
        slide(homeSnapshot(), appShot, false, home, leaving);
    } else {
        slide(appShot, homeSnapshot(), true, home, leaving);
    }
}

void sleep() {
    if (!inAmbient && !transition) enterAmbient();
}

// The clock screen swapped for what was open (home or an app) while dark
void finishWake() {
    waking = false;
    inAmbient = false;
    lv_display_trigger_activity(lv_display_get_default());  // or the idle timer sends it right back
    screens::tickHome();
    lv_screen_load(appScreen ? appScreen : home);
    screens::hideAmbient();
    if (ccScreen) {  // woken in the dip to the clock screen, before it dropped the control centre
        lv_obj_delete(ccScreen);
        ccScreen = nullptr;
    }
}

// Dip to black, swap while dark, rise to full brightness
void wake() {
    if (!inAmbient || transition || waking) return;
    setCpuMhz(kFullMhz);  // before anything redraws or moves
    waking = true;
    lv_display_trigger_activity(lv_display_get_default());
    auto rise = [](lv_anim_t *) {
        finishWake();
        fadeBrightness(settings::get().brightness, kWakeRiseMs);
    };
    if (display::brightness() == 0) {  // the clock screen is off: nothing to dip
        rise(nullptr);
    } else {
        fadeBrightness(0, kWakeDipMs, rise);
    }
}

void unlockShown(bool shown) {
    if (shown) setCpuMhz(kFullMhz);  // the knob follows the finger smoothly
    const auto &s = settings::get();
    if (inAmbient && !ringPanel && s.ambientMode == settings::AmbientMode::ScreenOff) {
        fadeBrightness(shown ? s.ambientBrightness : 0, kWakeMs);
    }
}

void applySettings() {
    screens::refreshHome();
    screens::refreshAmbient();
    applyPerfOverlay();
}

void printState() {
    Serial.printf("ui: active %p, home %p, app %p, ambient %p, transition %p%s, control centre %p\n",
                  lv_screen_active(), home, appScreen, ambient, transition,
                  lv_screen_active() == transition ? " (sliding)" : "", ccScreen);
}

lv_draw_buf_t *snapshot(lv_obj_t *obj) { return takeSnapshot(obj); }

void slideChange(lv_draw_buf_t *before, bool forward) {
    if (transition || inAmbient || !before) {  // busy: just show the change
        if (before) lv_draw_buf_destroy(before);
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_draw_buf_t *after = takeSnapshot(scr);
    changeShots[0] = before;
    changeShots[1] = after;
    if (forward) {
        slide(before, after, true, scr, nullptr);
    } else {
        slide(after, before, false, scr, nullptr);
    }
}

void showUpdateProgress(int percent) {
    lv_obj_t *&panel = updatePanel;
    static lv_obj_t *bar = nullptr;
    static lv_obj_t *label = nullptr;
    static int shown = -1;
    if (percent < 0) {  // failed: the old firmware carries on, and the screen is usable again
        if (panel) lv_obj_delete(panel);
        panel = bar = label = nullptr;
        shown = -1;
        return;
    }
    if (!panel) {
        setCpuMhz(kFullMhz);  // from the clock screen at 80 MHz, a cable update took twice as long
        display::setBrightness(settings::get().brightness);  // even from the ambient screen
        panel = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(panel);
        lv_obj_set_size(panel, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(panel, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
        label = lv_label_create(panel);
        lv_obj_set_style_text_font(label, &font_ui_30, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(theme::kText), 0);
        lv_obj_align(label, LV_ALIGN_CENTER, 0, -30);
        bar = lv_bar_create(panel);
        lv_obj_set_size(bar, 320, 12);
        lv_obj_align(bar, LV_ALIGN_CENTER, 0, 30);
    }
    if (percent == shown) return;
    shown = percent;
    lv_bar_set_value(bar, percent, LV_ANIM_OFF);
    lv_label_set_text_fmt(label, "Updating firmware… %d%%", percent);
    lv_refr_now(nullptr);
}

}  // namespace ui
