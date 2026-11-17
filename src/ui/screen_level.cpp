// Level: the motion sensor as a spirit level (design/src/screens/Level.tsx).
// Left to right big, with the angle; front to back in a smaller tube under it.
// The accelerometer runs only while this screen is open.

#include <Arduino.h>
#include <math.h>

#include "app/settings.h"
#include "hal/imu.h"
#include "hal/pins.h"
#include "screens.h"
#include "theme.h"

namespace ui::screens {
namespace {

constexpr float kLevelWithin = 0.3f;  // degrees: green, "Level"
constexpr float kTubeDegrees = 10;    // a bubble reaches its tube's end at this tilt
constexpr float kSmoothing = 0.15f;   // of each new reading, 25 times a second
constexpr uint32_t kTickMs = 40;
// The figures change only once the reading has moved this far: the sensor's
// last bit of noise otherwise flickers the tenths
constexpr float kSteady = 0.08f;
// Left to right needs gravity along the screen: lying flat (leaning more than
// about 60°) there's too little of it, and its direction is noise
constexpr float kUprightG = 0.5f;

constexpr uint32_t kTubeColour = 0x282B1E;  // the surface with a tenth of the bubble's colour
constexpr uint32_t kBubble = 0xDCF28A, kBubbleDark = 0x9CBF2E;
constexpr uint32_t kBubbleLevel = 0x5BE07D, kBubbleLevelDark = 0x23A045;

struct Tube {
    lv_obj_t *root, *middle, *bubble;
    int32_t reach;  // px from the middle to a bubble's end stop
    bool level;
};

lv_obj_t *body;
lv_obj_t *angleLabel, *sideLabel, *frontLabel;
Tube big, small;
float tiltX, tiltY;  // degrees, smoothed and without the offsets: negative = left side / front lower
bool haveReading = false;
bool upright = true;  // enough gravity along the screen to tell left from right
float steadyX, steadyY;  // what the figures show, kSteady behind the readings
int shownLevelX = -1, shownLevelY = -1;  // the colours shown: changed only when they change

lv_obj_t *box(lv_obj_t *parent, int32_t w, int32_t h, uint32_t colour, int32_t radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(colour), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    return o;
}

void colourBubble(lv_obj_t *bubble, bool level) {
    lv_obj_set_style_bg_color(bubble, lv_color_hex(level ? kBubbleLevel : kBubble), 0);
    lv_obj_set_style_bg_grad_color(bubble, lv_color_hex(level ? kBubbleLevelDark : kBubbleDark), 0);
}

// A rounded glass with three marks, the bubble in the middle; `ends` labels its two ends
Tube addTube(lv_obj_t *parent, int32_t height, int32_t bubbleW, int32_t bubbleH, int32_t gap,
             const char *const *ends) {
    Tube t{};
    t.root = box(parent, LV_PCT(100), height, kTubeColour, height / 2);
    lv_obj_set_style_border_width(t.root, 2, 0);
    lv_obj_set_style_border_color(t.root, lv_color_hex(theme::kSurfaceHi), 0);
    lv_obj_set_style_clip_corner(t.root, true, 0);
    if (ends) {
        for (int i = 0; i < 2; i++) {
            lv_obj_t *l = addText(t.root, ends[i], &font_ui_20, theme::kTextDim);
            lv_obj_align(l, i ? LV_ALIGN_RIGHT_MID : LV_ALIGN_LEFT_MID, i ? -20 : 20, 0);
        }
    }
    const int32_t lineH = height - 2 * 2 - 18;
    for (int i = -1; i <= 1; i++) {
        lv_obj_t *line = box(t.root, 3, lineH, theme::kTextDim, 2);
        lv_obj_align(line, LV_ALIGN_CENTER, i * gap, 0);
        if (i == 0) t.middle = line;
    }
    t.bubble = box(t.root, bubbleW, bubbleH, kBubble, bubbleH / 2);
    lv_obj_set_style_bg_grad_dir(t.bubble, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(t.bubble, LV_OPA_90, 0);
    lv_obj_align(t.bubble, LV_ALIGN_CENTER, 0, 0);
    // The glint of light on it
    lv_obj_t *glint = box(t.bubble, bubbleW * 2 / 5, bubbleH / 4, 0xFFFFFF, bubbleH / 8);
    lv_obj_set_style_bg_opa(glint, LV_OPA_50, 0);
    lv_obj_align(glint, LV_ALIGN_TOP_LEFT, bubbleW / 5, bubbleH / 6);
    colourBubble(t.bubble, false);
    return t;
}

void showTube(Tube &t, float tilt) {
    // It floats to the higher side: left when the right side (or the back) is lower
    const float along = LV_CLAMP(-1.0f, -tilt / kTubeDegrees, 1.0f);
    const int32_t x = lroundf(along * t.reach);
    if (lv_obj_get_style_translate_x(t.bubble, LV_PART_MAIN) != x) lv_obj_set_style_translate_x(t.bubble, x, 0);
    const bool level = fabsf(tilt) < kLevelWithin;
    if (level != t.level) {
        t.level = level;
        colourBubble(t.bubble, level);
        lv_obj_set_style_bg_color(t.middle, lv_color_hex(level ? theme::kGreen : theme::kTextDim), 0);
    }
}

// Follows `to` only when it has moved more than kSteady from what's shown
float steady(float shown, float to) {
    float d = to - shown;
    if (d > 180) d -= 360;  // left to right goes all the way round
    if (d < -180) d += 360;
    return fabsf(d) < kSteady ? shown : to;
}

void show() {
    const auto &s = settings::get();
    float x = tiltX - s.levelOffsetX / 100.0f;
    if (x > 180) x -= 360;
    if (x < -180) x += 360;
    steadyX = steady(steadyX, x);
    steadyY = steady(steadyY, tiltY - s.levelOffsetY / 100.0f);
    x = steadyX;
    const float y = steadyY;
    const bool levelX = upright && fabsf(x) < kLevelWithin, levelY = fabsf(y) < kLevelWithin;
    char text[32];
    snprintf(text, sizeof(text), "%.1f°", fabsf(x));
    setTextIfChanged(angleLabel, upright ? text : "-");
    setTextIfChanged(sideLabel, !upright ? "Stand the radio up"
                                : levelX ? "Level"
                                : x < 0  ? "Left side lower"
                                         : "Right side lower");
    lv_obj_set_hidden(big.bubble, !upright);
    if (levelX != shownLevelX) {
        shownLevelX = levelX;
        lv_obj_set_style_text_color(angleLabel, lv_color_hex(levelX ? theme::kGreen : theme::kText), 0);
        lv_obj_set_style_text_color(sideLabel, lv_color_hex(levelX ? theme::kGreen : theme::kTextDim), 0);
    }
    snprintf(text, sizeof(text), "%.1f° %s", fabsf(y), levelY ? "level" : y < 0 ? "front lower" : "back lower");
    setTextIfChanged(frontLabel, text);
    if (levelY != shownLevelY) {
        shownLevelY = levelY;
        lv_obj_set_style_text_color(frontLabel, lv_color_hex(levelY ? theme::kGreen : theme::kTextDim), 0);
    }
    showTube(big, x);
    showTube(small, y);
}

void tick(lv_timer_t *) {
    float gx, gy, gz;
    if (!imu::read(gx, gy, gz)) return;
    // Nothing yet, the moment after it starts (or a bad read): gravity is about 1 g
    if (gx * gx + gy * gy + gz * gz < 0.25f) return;
    // Left to right: the turn in the screen's plane, all the way round (0° upright, 90° on its
    // side, 180° upside down); up leans left when the right side is lower. Front to back: how
    // far up leans out of the screen's plane, whichever way the radio is turned in it; it
    // leans back when the front is lower.
    const float along = sqrtf(gx * gx + gy * gy);
    // A little give, so it doesn't blink on and off right at the edge
    upright = along > (upright ? kUprightG - 0.05f : kUprightG + 0.05f);
    const float x = atan2f(-gx, gy) * 180 / PI;
    const float y = atan2f(gz, along) * 180 / PI;
    if (!haveReading) {
        tiltX = x;
        tiltY = y;
        steadyX = x - settings::get().levelOffsetX / 100.0f;
        steadyY = y - settings::get().levelOffsetY / 100.0f;
        haveReading = true;
    } else {
        // Across ±180° (upside down) it would swing round the long way
        float dx = x - tiltX;
        if (dx > 180) dx -= 360;
        if (dx < -180) dx += 360;
        tiltX += dx * kSmoothing;
        if (tiltX > 180) tiltX -= 360;
        if (tiltX < -180) tiltX += 360;
        tiltY += (y - tiltY) * kSmoothing;
    }
    show();
}

void buildMissing() {
    lv_obj_t *col = addColumn(body, 12);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(col, 60, 0);
    addText(col, SYM_RULER, &font_ui_50, theme::kTextDim);
    addText(col, "No motion sensor found", &font_ui_22, theme::kText);
    addHint(col, "The radio couldn't reach its motion sensor when it started.");
}

}  // namespace

lv_obj_t *createLevel() {
    body = createAppScreen("Level");
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, 6, 0);
    lv_obj_add_event_cb(
        body,
        [](lv_event_t *) {
            imu::stop();
            body = nullptr;
        },
        LV_EVENT_DELETE, nullptr);
    if (!imu::present()) {
        buildMissing();
        return lv_obj_get_screen(body);
    }

    angleLabel = addText(body, "", &font_clock_96, theme::kText);
    lv_obj_set_style_text_line_space(angleLabel, 0, 0);
    sideLabel = addText(body, "", &font_ui_22, theme::kTextDim);

    big = addTube(body, 76, 84, 44, 46, nullptr);
    lv_obj_set_style_margin_top(big.root, 8, 0);
    lv_obj_set_style_margin_bottom(big.root, 10, 0);
    big.reach = 158;

    lv_obj_t *row = addRow(body, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(row, 8, 0);
    addText(row, "Front to back", &font_ui_22, theme::kTextDim);
    frontLabel = addText(row, "", &font_ui_22, theme::kTextDim);

    static const char *const kEnds[] = {"Front", "Back"};
    small = addTube(body, 52, 60, 30, 34, kEnds);
    lv_obj_set_style_margin_bottom(small.root, 12, 0);
    small.reach = 168;

    lv_obj_t *buttons = addRow(body, 10);
    lv_obj_t *set = addPill(buttons, LV_SYMBOL_OK "  Set as level", false, 54, [](lv_event_t *) {
        if (!haveReading) return;
        // Within the range a sensor could be off by: not a level radio on its side
        // Lying flat, left to right is noise: that one stays as it was
        if (upright) settings::get().levelOffsetX = lroundf(LV_CLAMP(-20.0f, tiltX, 20.0f) * 100);
        settings::get().levelOffsetY = lroundf(LV_CLAMP(-20.0f, tiltY, 20.0f) * 100);
        settings::changed();
        show();
    });
    lv_obj_set_flex_grow(set, 1);
    addPill(buttons, "Reset", false, 54, [](lv_event_t *) {
        settings::get().levelOffsetX = settings::get().levelOffsetY = 0;
        settings::changed();
        if (haveReading) show();
    });

    big.level = small.level = false;
    upright = true;
    shownLevelX = shownLevelY = -1;
    haveReading = false;
    imu::start();
    setTextIfChanged(angleLabel, "-");  // till the first reading, a moment later
    every(body, kTickMs, tick);
    return lv_obj_get_screen(body);
}

}  // namespace ui::screens
