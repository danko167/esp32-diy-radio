// The ambient screen's faces from the microSD card (card_faces.h), made on the
// upload page's face editor: design/src/faceFile.ts has the format, and they
// are drawn as design/src/screens/CustomFace.tsx does. ambient_faces.cpp builds
// the face's root and calls in here for the rest.

#include "ambient_faces.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <vector>

#include "app/psram_json.h"
#include "app/settings.h"
#include "card_faces.h"
#include "hal/pins.h"
#include "net/weather.h"
#include "screens.h"
#include "theme.h"

namespace ui::screens::ambient {
namespace {

// In the order of Piece::format
const char *const kDateFormats[] = {"long", "short", "day", "date", "numeric"};

bool parseHex(const char *s, uint32_t &rgb) {
    char *end;
    rgb = strtoul(s + 1, &end, 16);
    return s[0] == '#' && strlen(s) == 7 && !*end;
}

// "accent", "text", "dim" or "#rrggbb"; anything else is the text colour
uint32_t colourOf(JsonVariantConst c, uint32_t accent) {
    const char *s = c | "text";
    uint32_t rgb;
    if (!strcmp(s, "accent")) return accent;
    if (!strcmp(s, "dim")) return theme::kTextDim;
    return parseHex(s, rgb) ? rgb : theme::kText;
}

// The radio's text font nearest `size`, for what has to be text
const lv_font_t *uiFontFor(int32_t size) {
    return size < 21 ? &font_ui_20 : size < 26 ? &font_ui_22 : size < 40 ? &font_ui_30 : &font_ui_50;
}

// The radio's fonts by name, or a font file of the face's, loaded once each
// (`loaded` keeps them by file name). `size` is the font's nominal size.
const lv_font_t *fontOf(Face &f, uint16_t card, JsonVariantConst ref, std::vector<String> &loaded,
                        int32_t &size) {
    static const struct {
        const char *name;
        const lv_font_t *font;
        int32_t size;
    } kNamed[] = {{"clock96", &font_clock_96, 96}, {"clock140", &font_clock_140, 140}, {"ui20", &font_ui_20, 20},
                  {"ui22", &font_ui_22, 22},       {"ui30", &font_ui_30, 30},          {"ui50", &font_ui_50, 50}};
    if (ref.is<const char *>()) {
        for (const auto &n : kNamed) {
            if (!strcmp(ref.as<const char *>(), n.name)) {
                size = n.size;
                return n.font;
            }
        }
    }
    size = LV_CLAMP(8, ref["size"] | 30, 400);
    const char *file = ref["file"] | "";
    for (size_t i = 0; i < loaded.size(); i++) {
        if (loaded[i] == file) return f.fonts[i];
    }
    lv_font_t *font = card_faces::loadFont(card, file);
    if (!font) return uiFontFor(size);  // a missing or broken file: the face still shows
    loaded.push_back(file);
    f.fonts.push_back(font);
    return font;
}

// Time, date, alarm, weather or text, with its alignment point at x and its top at y
void buildPiece(Face &f, JsonObjectConst e, const lv_font_t *font, int32_t size, uint32_t colour) {
    const char *kind = e["kind"] | "";
    Piece p = {};
    p.dial = -1;
    if (!strcmp(kind, "time")) {
        p.kind = Piece::Time;
    } else if (!strcmp(kind, "date")) {
        p.kind = Piece::Date;
    } else if (!strcmp(kind, "alarm")) {
        p.kind = Piece::Alarm;
    } else if (!strcmp(kind, "weather")) {
        p.kind = Piece::Weather;
    } else if (strcmp(kind, "text") != 0) {
        return;  // from a later format: left out
    }
    const bool fixed = !strcmp(kind, "text");
    const bool time = p.kind == Piece::Time && !fixed;
    p.box = makeBox(f.root, LV_FLEX_FLOW_ROW, time ? 6 : LV_MAX(4, size / 4),
                    time ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_CENTER);
    const char *align = e["align"] | "left";
    // Up to a screen off each side: anything past that would overflow LVGL's sums
    const int32_t x = LV_CLAMP(-LCD_WIDTH, e["x"] | 0, 2 * LCD_WIDTH);
    const int32_t y = LV_CLAMP(-LCD_HEIGHT, e["y"] | 0, 2 * LCD_HEIGHT);
    if (!strcmp(align, "centre")) {
        lv_obj_align(p.box, LV_ALIGN_TOP_MID, x - LCD_WIDTH / 2, y);
    } else if (!strcmp(align, "right")) {
        lv_obj_align(p.box, LV_ALIGN_TOP_RIGHT, x - LCD_WIDTH, y);
    } else {
        lv_obj_align(p.box, LV_ALIGN_TOP_LEFT, x, y);
    }
    // Icons always from the radio's icon fonts, near the text's size
    if (!fixed && p.kind == Piece::Alarm) {
        p.icon = makeLabel(p.box, uiFontFor(size), colour);
        lv_label_set_text(p.icon, LV_SYMBOL_BELL);
    } else if (!fixed && p.kind == Piece::Weather) {
        const int32_t icon = size * 6 / 5;
        p.icon = makeLabel(p.box, icon < 37 ? &font_weather_30 : icon < 82 ? &font_weather_44 : &font_weather_120,
                           colour);
    }
    p.text = makeLabel(p.box, font, colour);
    if (fixed) {
        lv_label_set_text(p.text, e["text"] | "");
        return;  // never changes
    }
    if (time) {
        p.seconds = e["seconds"] | false;
        // Seconds small and dim, on the time's baseline (the row lines up bottoms)
        p.suffix = makeLabel(p.box, &font_ui_30, theme::kTextDim);
        const int32_t lift = font_ui_30.base_line - font->base_line;
        lv_obj_set_style_pad_bottom(lift > 0 ? p.text : p.suffix, LV_ABS(lift), 0);
        lv_obj_set_hidden(p.suffix, true);
    } else if (p.kind == Piece::Date) {
        const char *format = e["format"] | "long";
        for (size_t i = 0; i < sizeof(kDateFormats) / sizeof(kDateFormats[0]); i++) {
            if (!strcmp(format, kDateFormats[i])) p.format = i;
        }
    } else if (p.kind == Piece::Weather) {
        p.words = e["words"] | false;
        lv_obj_set_hidden(p.box, true);  // until there is a forecast
    } else {
        lv_obj_set_hidden(p.box, true);  // until there is an alarm
    }
    f.pieces.push_back(p);
}

// Ticks, hands and the centre dot, as lv_lines like the built-in Analog's
void buildDial(Face &f, JsonObjectConst e, Dial &d, uint32_t accent) {
    const int32_t radius = LV_CLAMP(10, e["radius"] | 150, LCD_WIDTH);
    JsonObjectConst hour = e["hour"], minute = e["minute"], second = e["second"];
    d.hourLength = LV_CLAMP(0, hour["length"] | 80, LCD_WIDTH);
    d.minuteLength = LV_CLAMP(0, minute["length"] | 125, LCD_WIDTH);
    d.secondLength = LV_CLAMP(0, second["length"] | 0, LCD_WIDTH);
    const int32_t widest = LV_CLAMP(6, LV_MAX(hour["width"] | 0, minute["width"] | 0), 40);  // as hand() has them
    // The second hand's tail sticks out 20 px behind the centre
    d.centre = LV_MAX(LV_MAX(radius, d.hourLength), LV_MAX(d.minuteLength, d.secondLength)) + widest + 2;
    lv_obj_t *dial = lv_obj_create(f.root);
    lv_obj_remove_style_all(dial);
    lv_obj_set_size(dial, 2 * d.centre + 1, 2 * d.centre + 1);
    const int32_t cx = LV_CLAMP(-LCD_WIDTH, e["cx"] | LCD_WIDTH / 2, 2 * LCD_WIDTH);
    const int32_t cy = LV_CLAMP(-LCD_HEIGHT, e["cy"] | LCD_HEIGHT / 2, 2 * LCD_HEIGHT);
    lv_obj_set_pos(dial, cx - d.centre, cy - d.centre);
    lv_obj_set_scrollable(dial, false);
    lv_obj_set_clickable(dial, false);

    const char *ticks = e["ticks"] | "none";
    const int count = !strcmp(ticks, "quarters") ? 4 : !strcmp(ticks, "hours") ? 12 : !strcmp(ticks, "minutes") ? 60 : 0;
    const uint32_t tickColour = colourOf(e["tickColour"], accent);
    d.ticks.resize(count);  // before the lines, which keep pointers to their points
    for (int i = 0; i < count; i++) {
        const int32_t angle = i * 360 / count;
        const bool major = angle % 90 == 0;
        d.ticks[i].line = makeLine(dial, major ? 6 : 3, tickColour);
        int32_t x1, y1, x2, y2;
        pointAt(d.centre, angle, radius - (major ? 26 : count == 60 ? 8 : 16), x1, y1);
        pointAt(d.centre, angle, radius, x2, y2);
        placeStroke(d.ticks[i], x1, y1, x2, y2);
    }
    auto hand = [&](Stroke &s, JsonObjectConst h, int32_t width) {
        s.line = makeLine(dial, LV_CLAMP(1, h["width"] | width, 40), colourOf(h["colour"], accent));
        lv_obj_set_hidden(s.line, true);  // until the clock is set
    };
    hand(d.hour, hour, 10);
    hand(d.minute, minute, 6);
    if (!second.isNull()) hand(d.second, second, 2);
    if (!e["dot"].isNull()) {
        lv_obj_t *dot = lv_obj_create(dial);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 16, 16);
        lv_obj_set_pos(dot, d.centre - 8, d.centre - 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(colourOf(e["dot"], accent)), 0);
        lv_obj_set_clickable(dot, false);
    }
}

void placeDialHand(Dial &d, Stroke &hand, int32_t angle, int32_t length, int32_t tail = 0) {
    int32_t x1, y1, x2, y2;
    pointAt(d.centre, angle + 180, tail, x1, y1);
    pointAt(d.centre, angle, length, x2, y2);
    placeStroke(hand, x1, y1, x2, y2);
}

}  // namespace

bool buildCard(Face &f, lv_obj_t *parent, uint16_t card) {
    JsonDocument doc(psram_json::allocator());
    if (!card_faces::load(card, doc)) {
        Serial.printf("faces: face %u won't load, Digital instead\n", card);
        return false;
    }
    // The background: a colour, or the picture (loaded only while it shows)
    f.backdrop = lv_obj_create(parent);
    lv_obj_remove_style_all(f.backdrop);
    lv_obj_set_size(f.backdrop, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_opa(f.backdrop, LV_OPA_COVER, 0);
    uint32_t background;
    if (!parseHex(doc["background"]["colour"] | "", background)) background = theme::kBg;
    lv_obj_set_style_bg_color(f.backdrop, lv_color_hex(background), 0);
    lv_obj_set_clickable(f.backdrop, false);
    lv_obj_set_scrollable(f.backdrop, false);
    lv_obj_move_background(f.backdrop);
    if (doc["background"]["picture"].is<const char *>()) f.picture = lv_image_create(f.backdrop);

    lv_obj_set_size(f.root, LCD_WIDTH, LCD_HEIGHT);
    JsonArrayConst elements = doc["elements"];
    size_t dials = 0;
    for (JsonObjectConst e : elements) dials += !strcmp(e["kind"] | "", "analog");
    f.dials.resize(dials);
    f.pieces.reserve(elements.size());
    std::vector<String> loaded;
    dials = 0;
    for (JsonObjectConst e : elements) {
        if (!strcmp(e["kind"] | "", "analog")) {
            buildDial(f, e, f.dials[dials], f.accent);
            Piece p = {};
            p.kind = Piece::Analog;
            p.dial = dials++;
            f.pieces.push_back(p);
            continue;
        }
        int32_t size;
        const lv_font_t *font = fontOf(f, card, e["font"], loaded, size);
        buildPiece(f, e, font, size, colourOf(e["colour"], f.accent));
    }
    return true;
}

void loadPicture(Face &f) {
    if (!f.picture || f.pixels) return;
    f.pixels = static_cast<uint16_t *>(heap_caps_malloc(card_faces::kBackgroundBytes, MALLOC_CAP_SPIRAM));
    if (!f.pixels) return;
    if (!card_faces::readBackground(f.card, f.pixels)) {
        heap_caps_free(f.pixels);
        f.pixels = nullptr;
        return;  // the background colour, then
    }
    f.image = {};
    f.image.header.magic = LV_IMAGE_HEADER_MAGIC;
    f.image.header.cf = LV_COLOR_FORMAT_RGB565;
    f.image.header.w = LCD_WIDTH;
    f.image.header.h = LCD_HEIGHT;
    f.image.header.stride = LCD_WIDTH * 2;
    f.image.data_size = card_faces::kBackgroundBytes;
    f.image.data = reinterpret_cast<const uint8_t *>(f.pixels);
    lv_image_set_src(f.picture, &f.image);
}

void freePicture(Face &f) {
    if (!f.pixels) return;
    lv_image_set_src(f.picture, nullptr);
    heap_caps_free(f.pixels);
    f.pixels = nullptr;
}

void showCardTime(Face &f, const struct tm &t) {
    char text[12];
    formatTime(text, sizeof(text), t);
    for (Piece &p : f.pieces) {
        if (p.kind == Piece::Time) setTextIfChanged(p.text, text);
        if (p.kind != Piece::Analog) continue;
        Dial &d = f.dials[p.dial];
        placeDialHand(d, d.hour, (t.tm_hour % 12) * 30 + t.tm_min / 2, d.hourLength);
        placeDialHand(d, d.minute, t.tm_min * 6, d.minuteLength);
        setHidden(d.hour.line, false);
        setHidden(d.minute.line, false);
    }
}

void showCardDetails(Face &f, const struct tm &t, const settings::Settings &s, const char *alarm) {
    char text[48], month[16];
    const bool newSecond = t.tm_sec != f.shownSecond;
    f.shownSecond = t.tm_sec;
    // nextAlarmText()'s has the bell, which has a label of its own here
    const char *bell = LV_SYMBOL_BELL " ";
    if (!strncmp(alarm, bell, strlen(bell))) alarm += strlen(bell);
    for (Piece &p : f.pieces) {
        switch (p.kind) {
            case Piece::Time:
                text[0] = '\0';
                if (p.seconds && s.ambientSeconds) snprintf(text, sizeof(text), "%02d", t.tm_sec);
                setTextIfChanged(p.suffix, text);
                setHidden(p.suffix, !text[0]);
                break;
            case Piece::Date:
                switch (p.format) {
                    case 0: formatDate(text, sizeof(text), t); break;  // Sunday, 1 November
                    case 1:
                        strftime(month, sizeof(month), "%b", &t);
                        snprintf(text, sizeof(text), "%s %d %s", dayName(t.tm_wday, true), t.tm_mday, month);
                        break;
                    case 2: strlcpy(text, dayName(t.tm_wday, false), sizeof(text)); break;
                    case 3:
                        strftime(month, sizeof(month), "%B", &t);
                        snprintf(text, sizeof(text), "%d %s", t.tm_mday, month);
                        break;
                    default: snprintf(text, sizeof(text), "%d. %d.", t.tm_mday, t.tm_mon + 1); break;
                }
                setTextIfChanged(p.text, text);
                break;
            case Piece::Alarm:
                setTextIfChanged(p.text, alarm);
                setHidden(p.box, !alarm[0]);
                break;
            case Piece::Analog: {
                Dial &d = f.dials[p.dial];
                if (!d.second.line) break;
                // As the built-in Analog's: moved only when the second changes
                if (s.ambientSeconds && newSecond) placeDialHand(d, d.second, t.tm_sec * 6, d.secondLength, 20);
                setHidden(d.second.line, !s.ambientSeconds);
                break;
            }
            default: break;
        }
    }
}

void showCardWeather(Face &f, const weather::Forecast *forecast) {
    for (Piece &p : f.pieces) {
        if (p.kind != Piece::Weather) continue;
        if (forecast) {
            lv_label_set_text(p.icon, weather::icon(forecast->now.code, forecast->now.isDay));
            char temp[16];
            formatTemp(temp, sizeof(temp), forecast->now.temp);
            if (p.words) {
                lv_label_set_text_fmt(p.text, "%s  %s", temp, weather::describe(forecast->now.code));
            } else {
                lv_label_set_text(p.text, temp);
            }
        }
        lv_obj_set_hidden(p.box, !forecast);
    }
}

void showCardNoTime(Face &f) {
    for (Piece &p : f.pieces) {
        if (p.kind == Piece::Time) {
            setTextIfChanged(p.text, "--:--");
            setHidden(p.suffix, true);
        } else if (p.kind == Piece::Date) {
            setTextIfChanged(p.text, "");
        } else if (p.kind == Piece::Alarm) {
            setHidden(p.box, true);
        } else if (p.kind == Piece::Analog) {
            Dial &d = f.dials[p.dial];
            setHidden(d.hour.line, true);
            setHidden(d.minute.line, true);
            setHidden(d.second.line, true);
        }
    }
}

}  // namespace ui::screens::ambient
