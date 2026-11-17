// The ambient screen's clock faces: what they all share, and the built-in ones
// from design/src/screens/ClockFaces.tsx (+ clock-faces.css), picked by
// settings' clockFace: Digital, Analog, Split, Night and Segment. A face from
// the card is built and shown by ambient_card_face.cpp.

#include "ambient_faces.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "app/settings.h"
#include "net/weather.h"
#include "screens.h"
#include "theme.h"

namespace ui::screens::ambient {
namespace {

using settings::ClockFace;

// Night and Segment: dim, lighting little and keeping a dark room dark. Red,
// or an old terminal's green about as bright (green looks brighter than red
// at the same level, so it's lower). In the order of settings::NightColour.
struct NightColours {
    uint32_t time, line;
};
constexpr NightColours kNightColours[] = {{0xB3261E, 0x6E1712}, {0x13882F, 0x0A4518}};
constexpr int kUnlitPercent = 14;  // Segment's unlit segments, an old LED clock's faint "88:88"

// Analog dial, in the dial object's own pixels
constexpr int32_t kDialCentre = 165;
constexpr int32_t kDialRadius = 150;

uint32_t scaled(uint32_t rgb, int percent) {
    const uint32_t r = (rgb >> 16 & 0xFF) * percent / 100, g = (rgb >> 8 & 0xFF) * percent / 100,
                   b = (rgb & 0xFF) * percent / 100;
    return r << 16 | g << 8 | b;
}

// Time and, after it on the baseline side, the seconds
void makeTimeRow(Face &f, lv_obj_t *parent, const lv_font_t *font, uint32_t colour,
                 const lv_font_t *suffixFont, uint32_t suffixColour, int32_t suffixLift) {
    lv_obj_t *row = makeBox(parent, LV_FLEX_FLOW_ROW, 8, LV_FLEX_ALIGN_END);
    f.clock = makeLabel(row, font, colour);
    f.suffix = makeLabel(row, suffixFont, suffixColour);
    lv_obj_set_style_pad_bottom(f.suffix, suffixLift, 0);
}

// Icon and temperature, `padTop` apart from what is above. In the accent,
// except on the night faces, which keep to their own dim colour.
void makeWeather(Face &f, lv_obj_t *parent, int32_t padTop, const lv_font_t *iconFont = &font_weather_44,
                 const lv_font_t *textFont = &font_ui_30, uint32_t colour = theme::accent()) {
    f.weatherRow = makeBox(parent, LV_FLEX_FLOW_ROW, iconFont == &font_weather_44 ? 12 : 8, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(f.weatherRow, padTop, 0);
    f.weatherIcon = makeLabel(f.weatherRow, iconFont, colour);
    f.weatherText = makeLabel(f.weatherRow, textFont, colour);
    lv_obj_set_hidden(f.weatherRow, true);
}

// From the built-in Analog's centre. It hides the header's pointAt() in here,
// hence ambient::.
void pointAt(int32_t angle, int32_t length, int32_t &x, int32_t &y) {
    ambient::pointAt(kDialCentre, angle, length, x, y);
}

void placeHand(Stroke &hand, int32_t angle, int32_t length) {
    int32_t x, y;
    pointAt(angle, length, x, y);
    placeStroke(hand, kDialCentre, kDialCentre, x, y);
}

void buildDigital(Face &f) {
    lv_obj_set_flex_flow(f.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(f.root, 6, 0);
    makeTimeRow(f, f.root, &font_clock_140, theme::kText, &font_ui_30, theme::kTextDim, 10);
    f.date = makeLabel(f.root, &font_ui_30, theme::kTextDim);
    f.alarm = makeLabel(f.root, &font_ui_30, theme::kTextDim);
    makeWeather(f, f.root, 16);
}

void buildAnalog(Face &f) {
    lv_obj_set_flex_flow(f.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(f.root, 8, 0);
    lv_obj_t *dial = lv_obj_create(f.root);
    lv_obj_remove_style_all(dial);
    lv_obj_set_size(dial, 2 * kDialCentre, 2 * kDialCentre);
    lv_obj_set_scrollable(dial, false);
    lv_obj_set_clickable(dial, false);
    // Static: 12, 3, 6 and 9 longer and brighter
    for (int i = 0; i < 12; i++) {
        const bool major = i % 3 == 0;
        f.ticks[i].line = makeLine(dial, major ? 6 : 3, major ? theme::kText : theme::kTextDim);
        int32_t x1, y1, x2, y2;
        pointAt(i * 30, kDialRadius - (major ? 26 : 16), x1, y1);
        pointAt(i * 30, kDialRadius, x2, y2);
        placeStroke(f.ticks[i], x1, y1, x2, y2);
    }
    f.hourHand.line = makeLine(dial, 10, theme::kText);
    f.minuteHand.line = makeLine(dial, 6, theme::kText);
    f.secondHand.line = makeLine(dial, 2, theme::kRed);
    placeHand(f.hourHand, 0, 82);
    placeHand(f.minuteHand, 0, 124);
    placeHand(f.secondHand, 0, 136);
    lv_obj_set_hidden(f.hourHand.line, true);  // until the clock is set
    lv_obj_set_hidden(f.minuteHand.line, true);
    lv_obj_set_hidden(f.secondHand.line, true);
    f.dot = lv_obj_create(dial);
    lv_obj_remove_style_all(f.dot);
    lv_obj_set_size(f.dot, 16, 16);
    lv_obj_set_pos(f.dot, kDialCentre - 8, kDialCentre - 8);
    lv_obj_set_style_radius(f.dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(f.dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(f.dot, lv_color_hex(theme::accent()), 0);
    lv_obj_set_clickable(f.dot, false);
    // Under the 12, where watches print their name
    f.date = makeLabel(dial, &font_ui_22, theme::kTextDim);
    lv_obj_set_style_text_letter_space(f.date, 2, 0);
    lv_obj_align(f.date, LV_ALIGN_TOP_MID, 0, 82);

    lv_obj_t *info = makeBox(f.root, LV_FLEX_FLOW_ROW, 28, LV_FLEX_ALIGN_CENTER);
    f.alarm = makeLabel(info, &font_ui_30, theme::kTextDim);
    makeWeather(f, info, 0);
}

void buildSplit(Face &f) {
    lv_obj_set_flex_flow(f.root, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(f.root, 20, 0);
    // Baselines 150 px apart, as in the design; the font's line is 102
    lv_obj_t *time = makeBox(f.root, LV_FLEX_FLOW_COLUMN, 150 - lv_font_get_line_height(&font_clock_140),
                             LV_FLEX_ALIGN_START);
    makeTimeRow(f, time, &font_clock_140, theme::kText, &font_ui_30, theme::kTextDim, 10);
    f.minutes = makeLabel(time, &font_clock_140, theme::kTextDim);

    lv_obj_t *info = makeBox(f.root, LV_FLEX_FLOW_COLUMN, 8, LV_FLEX_ALIGN_START);
    f.day = makeLabel(info, &font_ui_30, theme::kText);
    f.date = makeLabel(info, &font_ui_30, theme::kTextDim);
    f.alarm = makeLabel(info, &font_ui_30, theme::kTextDim);
    makeWeather(f, info, 12);
}

void buildNight(Face &f) {
    const NightColours &c = kNightColours[static_cast<int>(f.colour)];
    lv_obj_set_flex_flow(f.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(f.root, 16, 0);  // the design's 10, plus its taller line box
    makeTimeRow(f, f.root, &font_clock_96, c.time, &font_ui_22, c.time, 6);
    f.date = makeLabel(f.root, &font_ui_22, c.line);
    makeWeather(f, f.root, 0, &font_weather_30, &font_ui_22, c.line);
}

// Night's, in LED digits over their own unlit segments. The font's digits
// are all as wide, so the time always covers "88:88".
void buildSegment(Face &f) {
    const NightColours &c = kNightColours[static_cast<int>(f.colour)];
    lv_obj_set_flex_flow(f.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(f.root, 10, 0);  // the design's: its line box is the font's
    lv_obj_t *row = makeBox(f.root, LV_FLEX_FLOW_ROW, 8, LV_FLEX_ALIGN_END);
    lv_obj_t *digits = lv_obj_create(row);
    lv_obj_remove_style_all(digits);
    lv_obj_set_size(digits, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_scrollable(digits, false);
    lv_obj_set_clickable(digits, false);
    lv_label_set_text(makeLabel(digits, &font_seg_112, scaled(c.time, kUnlitPercent)), "88:88");
    f.clock = makeLabel(digits, &font_seg_112, c.time);
    f.suffix = makeLabel(row, &font_ui_22, c.time);
    lv_obj_set_style_pad_bottom(f.suffix, 4, 0);
    f.date = makeLabel(f.root, &font_ui_22, c.line);
    makeWeather(f, f.root, 0, &font_weather_30, &font_ui_22, c.line);
}

}  // namespace

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, "");  // not LVGL's "Text"
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

lv_obj_t *makeBox(lv_obj_t *parent, lv_flex_flow_t flow, int32_t gap, lv_flex_align_t cross) {
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(box, flow);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_CENTER, cross, cross);
    lv_obj_set_style_pad_gap(box, gap, 0);
    lv_obj_set_clickable(box, false);  // taps go to the screen
    return box;
}

void placeStroke(Stroke &s, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    const int32_t pad = (lv_obj_get_style_line_width(s.line, LV_PART_MAIN) + 1) / 2;  // round ends
    const int32_t x = LV_MIN(x1, x2) - pad;
    const int32_t y = LV_MIN(y1, y2) - pad;
    s.points[0] = {x1 - x, y1 - y};
    s.points[1] = {x2 - x, y2 - y};
    lv_obj_set_pos(s.line, x, y);
    lv_obj_set_size(s.line, LV_ABS(x2 - x1) + 2 * pad + 1, LV_ABS(y2 - y1) + 2 * pad + 1);
    lv_line_set_points(s.line, s.points, 2);
}

lv_obj_t *makeLine(lv_obj_t *parent, int32_t width, uint32_t colour) {
    lv_obj_t *line = lv_line_create(parent);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(colour), 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    return line;
}

void pointAt(int32_t centre, int32_t angle, int32_t length, int32_t &x, int32_t &y) {
    const int16_t a = static_cast<int16_t>(angle % 360);
    x = centre + ((length * lv_trigo_sin(a) + (1 << (LV_TRIGO_SHIFT - 1))) >> LV_TRIGO_SHIFT);
    y = centre - ((length * lv_trigo_cos(a) + (1 << (LV_TRIGO_SHIFT - 1))) >> LV_TRIGO_SHIFT);
}

void buildClockFace(lv_obj_t *parent, ClockFace kind, uint16_t card, uint32_t cardVersion, Face &f) {
    f = Face{};
    f.kind = kind;
    f.want = kind;
    f.card = card;
    f.cardVersion = cardVersion;
    f.accent = theme::accent();
    f.colour = settings::nightColour();
    f.shownSecond = -1;
    f.root = lv_obj_create(parent);
    lv_obj_remove_style_all(f.root);
    lv_obj_set_size(f.root, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(f.root);
    lv_obj_set_flex_align(f.root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(f.root, false);
    lv_obj_set_clickable(f.root, false);  // taps go to the screen
    if (kind == ClockFace::Card && !buildCard(f, parent, card)) f.kind = ClockFace::Digital;
    switch (f.kind) {
        case ClockFace::Analog: buildAnalog(f); break;
        case ClockFace::Split: buildSplit(f); break;
        case ClockFace::Night: buildNight(f); break;
        case ClockFace::Segment: buildSegment(f); break;
        case ClockFace::Card: break;
        default: buildDigital(f); break;
    }
}

void destroyFace(Face &f) {
    if (f.root) lv_obj_delete(f.root);
    if (f.backdrop) lv_obj_delete(f.backdrop);  // before the picture it shows
    if (f.pixels) heap_caps_free(f.pixels);
    for (lv_font_t *font : f.fonts) lv_binfont_destroy(font);  // after the labels using them
    f = Face{};
}

void showTime(Face &f, const struct tm &t) {
    char text[12];
    if (f.kind == ClockFace::Card) {
        showCardTime(f, t);
    } else if (f.kind == ClockFace::Analog) {
        placeHand(f.hourHand, (t.tm_hour % 12) * 30 + t.tm_min / 2, 82);
        placeHand(f.minuteHand, t.tm_min * 6, 124);
        setHidden(f.hourHand.line, false);
        setHidden(f.minuteHand.line, false);
    } else if (f.kind == ClockFace::Split) {
        snprintf(text, sizeof(text), "%02d", t.tm_hour);
        setTextIfChanged(f.clock, text);
        snprintf(text, sizeof(text), "%02d", t.tm_min);
        setTextIfChanged(f.minutes, text);
    } else {
        formatTime(text, sizeof(text), t);
        setTextIfChanged(f.clock, text);
    }
}

void showDetails(Face &f, const struct tm &t, const settings::Settings &s, const char *alarm) {
    if (f.kind == ClockFace::Card) return showCardDetails(f, t, s, alarm);
    char text[48];
    // Seconds: Digital as digits, Analog as a red hand (its line object covers
    // just the hand, so each second redraws little); Split's and Night's big
    // digits (and Segment's) have no good place for them
    if (f.kind == ClockFace::Analog) {
        // Ticks come 4 times a second: move it only when the second changes
        if (s.ambientSeconds && t.tm_sec != f.shownSecond) {
            f.shownSecond = t.tm_sec;
            placeHand(f.secondHand, t.tm_sec * 6, 136);
        }
        setHidden(f.secondHand.line, !s.ambientSeconds);
    }
    text[0] = '\0';
    if (f.kind == ClockFace::Digital && s.ambientSeconds) snprintf(text, sizeof(text), "%02d", t.tm_sec);
    if (f.suffix) {
        setTextIfChanged(f.suffix, text);
        // An empty label would still take a flex gap and push the clock off centre
        setHidden(f.suffix, text[0] == '\0');
    }

    char month[16];
    switch (f.kind) {
        case ClockFace::Analog:
            snprintf(text, sizeof(text), "%s %d", dayName(t.tm_wday, true), t.tm_mday);
            for (char *c = text; *c; c++) *c = toupper(*c);
            break;
        case ClockFace::Split:
            setTextIfChanged(f.day, dayName(t.tm_wday, false));
            strftime(month, sizeof(month), "%B", &t);
            snprintf(text, sizeof(text), "%d %s", t.tm_mday, month);
            break;
        case ClockFace::Night:
        case ClockFace::Segment:
            // Date and alarm share the one dim line
            strftime(month, sizeof(month), "%b", &t);
            snprintf(text, sizeof(text), "%s %d %s%s%s", dayName(t.tm_wday, true), t.tm_mday, month,
                     alarm[0] ? " \u00B7 " : "", alarm);
            break;
        default:
            formatDate(text, sizeof(text), t);
            break;
    }
    setTextIfChanged(f.date, text);
    if (f.alarm) {
        setTextIfChanged(f.alarm, alarm);
        setHidden(f.alarm, !alarm[0]);
    }
}

void showWeather(Face &f, const weather::Forecast *forecast) {
    if (f.kind == ClockFace::Card) return showCardWeather(f, forecast);
    if (!f.weatherRow) return;
    if (forecast) {
        lv_label_set_text(f.weatherIcon, weather::icon(forecast->now.code, forecast->now.isDay));
        char temp[16];
        formatTemp(temp, sizeof(temp), forecast->now.temp);
        if (f.kind == ClockFace::Digital) {
            lv_label_set_text_fmt(f.weatherText, "%s  %s", temp, weather::describe(forecast->now.code));
        } else {
            lv_label_set_text(f.weatherText, temp);
        }
    }
    lv_obj_set_hidden(f.weatherRow, !forecast);
}

void showNoTime(Face &f) {
    if (f.kind == ClockFace::Card) return showCardNoTime(f);
    if (f.kind == ClockFace::Split) {
        setTextIfChanged(f.clock, "--");
        setTextIfChanged(f.minutes, "--");
    } else if (f.clock) {
        setTextIfChanged(f.clock, "--:--");
    }
    setHidden(f.hourHand.line, true);
    setHidden(f.minuteHand.line, true);
    setHidden(f.secondHand.line, true);
    if (f.suffix) setTextIfChanged(f.suffix, "");
    setHidden(f.suffix, true);
    setTextIfChanged(f.date, "");
    if (f.day) setTextIfChanged(f.day, "");
    if (f.alarm) setTextIfChanged(f.alarm, "");
    setHidden(f.alarm, true);
}

}  // namespace ui::screens::ambient
