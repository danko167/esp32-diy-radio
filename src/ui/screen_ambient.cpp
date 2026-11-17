// Ambient screen: shown after inactivity. Pure black with a big clock that
// moves a few pixels every minute, so no pixel stays lit in one place for hours.
// The face is settings' clockFace: ambient_faces.cpp has the built-in ones,
// ambient_card_face.cpp draws a face file from the microSD card.

#include <Arduino.h>

#include "ambient_faces.h"
#include "app/settings.h"
#include "app/storage.h"
#include "card_faces.h"
#include "hal/pins.h"
#include "net/weather.h"
#include "screens.h"
#include "theme.h"
#include "ui.h"

namespace ui::screens {
namespace {

using namespace ambient;
using settings::ClockFace;

constexpr int32_t kShiftX = 30;  // furthest from the middle
constexpr int32_t kShiftY = 60;
constexpr int32_t kShiftStep = 10;  // most it moves at once

Face live;  // the ambient screen's
bool liveShown = false;  // the ambient screen is up: a card face's picture is loaded
char shownTime[12];  // formatTime() of what the face shows; "" draws it again
uint32_t shownWeather = UINT32_MAX;
uint32_t lastCardLook = 0;  // when the clock screen last looked for a card put back

// Settings > Clock screen > Lock: a tap shows "slide to unlock" at the bottom
// (design/src/screens/LockScreen.tsx); the knob pulled to the end wakes it,
// let go before it springs back, and left alone it goes after 5 s. The
// buttons and a ringing alarm wake the radio as always.
constexpr int32_t kTrackWidth = 400, kTrackHeight = 70, kTrackPad = 6, kKnob = 58;
constexpr int32_t kTravel = kTrackWidth - 2 * kTrackPad - kKnob;
constexpr uint32_t kUnlockHideMs = 5000;
lv_obj_t *track, *knob, *lockText;
lv_timer_t *hideTimer;
int32_t knobX = 0;
int32_t pulse = LV_OPA_COVER;  // the words' slow glow

void showLockText() {
    lv_obj_set_style_opa(lockText, pulse * (kTravel - knobX) / kTravel, 0);
}

void moveKnob(int32_t x) {
    knobX = LV_CLAMP(0, x, kTravel);
    lv_obj_set_style_translate_x(knob, knobX, 0);
    showLockText();
}

void hideUnlock() {
    if (lv_obj_is_hidden(track)) return;
    lv_obj_set_hidden(track, true);
    lv_timer_pause(hideTimer);
    lv_anim_delete(knob, nullptr);
    lv_anim_delete(lockText, nullptr);
    moveKnob(0);
    ui::unlockShown(false);
}

void showUnlock() {
    lv_timer_reset(hideTimer);
    lv_timer_resume(hideTimer);
    if (!lv_obj_is_hidden(track)) return;
    lv_obj_set_hidden(track, false);
    ui::unlockShown(true);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, lockText);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_50);
    lv_anim_set_duration(&a, 1200);
    lv_anim_set_reverse_duration(&a, 1200);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, [](void *, int32_t v) {
        pulse = v;
        showLockText();
    });
    lv_anim_start(&a);
}

void onKnob(lv_event_t *e) {
    switch (lv_event_get_code(e)) {
        case LV_EVENT_PRESSED:
            lv_anim_delete(knob, nullptr);  // caught while springing back
            lv_timer_pause(hideTimer);
            break;
        case LV_EVENT_PRESSING: {
            lv_point_t v;
            lv_indev_get_vect(lv_indev_active(), &v);
            moveKnob(knobX + v.x);
            break;
        }
        case LV_EVENT_RELEASED:
        case LV_EVENT_PRESS_LOST:
            if (knobX >= kTravel * 9 / 10) {
                hideUnlock();
                ui::wake();
                break;
            }
            {
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, knob);
                lv_anim_set_values(&a, knobX, 0);
                lv_anim_set_duration(&a, 250);
                lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
                lv_anim_set_exec_cb(&a, [](void *, int32_t v) { moveKnob(v); });
                lv_anim_start(&a);
            }
            showUnlock();  // 5 s again from letting go
            break;
        default:
            break;
    }
}

void buildUnlock(lv_obj_t *scr) {
    track = lv_obj_create(scr);
    lv_obj_remove_style_all(track);
    lv_obj_set_size(track, kTrackWidth, kTrackHeight);
    lv_obj_align(track, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_radius(track, kTrackHeight / 2, 0);
    lv_obj_set_style_bg_color(track, lv_color_hex(theme::kSurface), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(track, 1, 0);
    lv_obj_set_style_border_color(track, lv_color_hex(theme::kSurfaceHi), 0);
    lv_obj_set_scrollable(track, false);
    lv_obj_set_clickable(track, true);  // a tap on it isn't the screen's
    lv_obj_add_event_cb(track, [](lv_event_t *) { showUnlock(); }, LV_EVENT_CLICKED, nullptr);

    lockText = lv_label_create(track);
    lv_label_set_text(lockText, "slide to unlock");
    lv_obj_set_style_text_font(lockText, &font_ui_22, 0);
    lv_obj_set_style_text_color(lockText, lv_color_hex(theme::kText), 0);
    lv_obj_align(lockText, LV_ALIGN_CENTER, (kKnob + kTrackPad) / 2, 0);

    knob = lv_obj_create(track);
    lv_obj_remove_style_all(knob);
    lv_obj_set_size(knob, kKnob, kKnob);
    lv_obj_align(knob, LV_ALIGN_LEFT_MID, kTrackPad - 1, 0);  // inside the 1 px border
    lv_obj_set_style_radius(knob, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(knob, lv_color_hex(theme::kSurfaceHi), 0);
    lv_obj_set_style_bg_opa(knob, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(knob, false);
    lv_obj_set_clickable(knob, true);
    lv_obj_set_gesture_bubble(knob, false);
    lv_obj_add_event_cb(knob, onKnob, LV_EVENT_ALL, nullptr);
    lv_obj_t *arrow = lv_label_create(knob);
    lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(arrow, &font_ui_22, 0);
    lv_obj_set_style_text_color(arrow, lv_color_hex(theme::kText), 0);
    lv_obj_center(arrow);

    lv_obj_set_hidden(track, true);
    hideTimer = lv_timer_create([](lv_timer_t *) { hideUnlock(); }, kUnlockHideMs, nullptr);
    lv_timer_pause(hideTimer);
}

// What settings ask for. A face from the card that's out or gone is Digital
// meanwhile (card 0), and comes back with it.
ClockFace faceSetting(uint16_t &card, uint32_t &version) {
    const auto &s = settings::get();
    card = 0;
    version = 0;
    if (s.clockFace == ClockFace::Card) {
        if (const card_faces::Face *f = card_faces::find(s.clockFaceFile)) {
            card = f->id;
            version = f->version;
            return ClockFace::Card;
        }
        return ClockFace::Digital;
    }
    return s.clockFace <= ClockFace::Segment ? s.clockFace : ClockFace::Digital;
}

bool outdated(const Face &f) {
    uint16_t card;
    uint32_t version;
    const ClockFace want = faceSetting(card, version);
    return want != f.want || card != f.card || version != f.cardVersion ||
           settings::nightColour() != f.colour || (want == ClockFace::Card && theme::accent() != f.accent);
}

int32_t wander(int32_t at, int32_t range) {
    const int32_t step = static_cast<int32_t>(lv_rand(0, 2 * kShiftStep)) - kShiftStep;
    return LV_CLAMP(-range, at + step, range);
}

// A small step in the same frame as the new minute, so the minute costs one
// redraw. (A slow glide redrew the whole face for seconds every minute.) The
// big faces get less room, so they never leave the screen.
void shiftFace(Face &f) {
    lv_obj_t *face = f.root;
    lv_obj_update_layout(face);
    int32_t rangeX = LV_CLAMP(0, (LCD_WIDTH - lv_obj_get_width(face)) / 2, kShiftX);
    int32_t rangeY = LV_CLAMP(0, (LCD_HEIGHT - lv_obj_get_height(face)) / 2, kShiftY);
    if (f.kind == ClockFace::Card) {
        // As far as the pieces have room to the edges, the nearer one; but always
        // a little, even for a piece at the edge, which may lose a few pixels then
        lv_area_t root, area, all = {LCD_WIDTH, LCD_HEIGHT, 0, 0};
        lv_obj_get_coords(face, &root);
        for (uint32_t i = 0; i < lv_obj_get_child_count(face); i++) {
            lv_obj_t *child = lv_obj_get_child(face, i);
            if (lv_obj_is_hidden(child)) continue;
            lv_obj_get_coords(child, &area);
            all = {LV_MIN(all.x1, area.x1 - root.x1), LV_MIN(all.y1, area.y1 - root.y1),
                   LV_MAX(all.x2, area.x2 - root.x1), LV_MAX(all.y2, area.y2 - root.y1)};
        }
        rangeX = LV_CLAMP(kShiftStep, LV_MIN(all.x1, LCD_WIDTH - 1 - all.x2), kShiftX);
        rangeY = LV_CLAMP(kShiftStep, LV_MIN(all.y1, LCD_HEIGHT - 1 - all.y2), kShiftY);
    }
    lv_obj_set_style_translate_x(face, wander(lv_obj_get_style_translate_x(face, LV_PART_MAIN), rangeX), 0);
    lv_obj_set_style_translate_y(face, wander(lv_obj_get_style_translate_y(face, LV_PART_MAIN), rangeY), 0);
}

// Averages each block of `from` into one pixel of `to` (both RGB565)
void shrink(const lv_draw_buf_t *from, lv_draw_buf_t *to) {
    const int32_t fw = from->header.w, fh = from->header.h, tw = to->header.w, th = to->header.h;
    for (int32_t y = 0; y < th; y++) {
        const int32_t y1 = y * fh / th, y2 = LV_MAX(y1 + 1, (y + 1) * fh / th);
        auto *out = reinterpret_cast<lv_color16_t *>(to->data + y * to->header.stride);
        for (int32_t x = 0; x < tw; x++) {
            const int32_t x1 = x * fw / tw, x2 = LV_MAX(x1 + 1, (x + 1) * fw / tw);
            uint32_t r = 0, g = 0, b = 0;
            for (int32_t sy = y1; sy < y2; sy++) {
                const auto *in = reinterpret_cast<const lv_color16_t *>(from->data + sy * from->header.stride);
                for (int32_t sx = x1; sx < x2; sx++) {
                    r += in[sx].red;
                    g += in[sx].green;
                    b += in[sx].blue;
                }
            }
            const uint32_t n = (y2 - y1) * (x2 - x1);
            out[x].red = (r + n / 2) / n;
            out[x].green = (g + n / 2) / n;
            out[x].blue = (b + n / 2) / n;
        }
    }
}

}  // namespace

lv_obj_t *createAmbient() {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(theme::kBg), 0);
    lv_obj_set_scrollable(scr, false);
    lv_obj_add_event_cb(
        scr,
        [](lv_event_t *) {
            if (settings::get().screenLock) {
                showUnlock();
            } else {
                ui::wake();
            }
        },
        LV_EVENT_CLICKED, nullptr);
    uint16_t card;
    uint32_t version;
    const ClockFace kind = faceSetting(card, version);
    buildClockFace(scr, kind, card, version, live);
    buildUnlock(scr);
    refreshAmbient();
    return scr;
}

void refreshAmbient() {
    const auto &s = settings::get();
    if (outdated(live)) {
        lv_obj_t *scr = lv_obj_get_parent(live.root);
        destroyFace(live);
        uint16_t card;
        uint32_t version;
        const ClockFace kind = faceSetting(card, version);
        buildClockFace(scr, kind, card, version, live);
        lv_obj_move_foreground(track);  // over the new face
    }
    if (liveShown) loadPicture(live);
    const bool off = s.ambientMode == settings::AmbientMode::ScreenOff;
    lv_obj_set_hidden(live.root, off);
    if (live.backdrop) lv_obj_set_hidden(live.backdrop, off);
    // The accent may have changed in Settings > Display (card faces are built again for it)
    const lv_color_t accent = lv_color_hex(theme::accent());
    if (live.weatherRow && live.kind != ClockFace::Night && live.kind != ClockFace::Segment) {
        lv_obj_set_style_text_color(live.weatherIcon, accent, 0);
        lv_obj_set_style_text_color(live.weatherText, accent, 0);
    }
    if (live.dot) lv_obj_set_style_bg_color(live.dot, accent, 0);
    shownWeather = UINT32_MAX;
    shownTime[0] = '\0';
    tickAmbient();
}

void showAmbient() {
    liveShown = true;
    // A face from a card that's out: maybe it's back (a short stall, while the screen is dark)
    if (settings::get().clockFace == ClockFace::Card && !storage::hasCard()) {
        lastCardLook = millis();
        storage::checkForCard();
    }
    refreshAmbient();
}

void hideAmbient() {
    liveShown = false;
    hideUnlock();
    freePicture(live);
}

bool unlockShowing() { return track && !lv_obj_is_hidden(track); }

void tickAmbient() {
    const auto &s = settings::get();
    // A card face that came, went or changed (from the upload page)
    if (outdated(live)) return refreshAmbient();
    struct tm t;
    if (localNow(t)) {
        char time[sizeof(shownTime)];
        formatTime(time, sizeof(time), t);
        if (strcmp(time, shownTime) != 0) {
            strlcpy(shownTime, time, sizeof(shownTime));
            showTime(live, t);
            if (lv_screen_active() == lv_obj_get_screen(live.root)) shiftFace(live);
            // The card isn't polled (looking for one stalls), but a clock screen
            // showing Digital for a card face that's out looks now and then
            if (liveShown && s.clockFace == ClockFace::Card && !storage::hasCard() &&
                millis() - lastCardLook >= 5 * 60 * 1000) {
                lastCardLook = millis();
                storage::checkForCard();
            }
        }
        char alarm[32] = "";
        nextAlarmText(alarm, sizeof(alarm));
        showDetails(live, t, s, alarm);
    } else {
        shownTime[0] = '\0';
        showNoTime(live);
    }

    // Weather, only in "Clock + weather" mode and once there is a forecast
    if (weather::revision() != shownWeather) {
        shownWeather = weather::revision();
        weather::Forecast f;
        const bool show = s.ambientMode == settings::AmbientMode::ClockWeather && weather::get(f);
        showWeather(live, show ? &f : nullptr);
    }
}

lv_draw_buf_t *clockFacePreview(settings::ClockFace kind, uint16_t card, bool withWeather, int32_t size) {
    const auto &s = settings::get();
    // The design's moment: Sunday 1 November 2026, 17:42, next alarm 07:30
    struct tm t = {};
    t.tm_year = 2026 - 1900;
    t.tm_mon = 10;
    t.tm_mday = 1;
    t.tm_wday = 0;
    t.tm_hour = 7;
    t.tm_min = 30;
    char clock[12], alarm[24];
    formatTime(clock, sizeof(clock), t);
    snprintf(alarm, sizeof(alarm), LV_SYMBOL_BELL " %s", clock);
    t.tm_hour = 17;
    t.tm_min = 42;
    t.tm_sec = 5;

    // On a screen of its own that is never shown, so nothing else redraws
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(theme::kBg), 0);
    lv_obj_set_scrollable(scr, false);
    Face f{};
    if (kind != ClockFace::Card && kind > ClockFace::Segment) kind = ClockFace::Digital;
    buildClockFace(scr, kind, card, 0, f);
    loadPicture(f);
    showTime(f, t);
    showDetails(f, t, s, alarm);
    weather::Forecast forecast;
    if (withWeather && !weather::get(forecast)) {
        forecast = {};
        forecast.now.code = 2;  // partly cloudy, 12°, as in the design
        forecast.now.isDay = true;
        forecast.now.temp = 12;
    }
    showWeather(f, withWeather ? &forecast : nullptr);

    lv_draw_buf_t *full = ui::snapshot(scr);
    destroyFace(f);
    lv_obj_delete(scr);
    if (!full) return nullptr;
    lv_draw_buf_t *small = lv_draw_buf_create(size, size, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
    if (small) shrink(full, small);
    lv_draw_buf_destroy(full);
    return small;
}

}  // namespace ui::screens
