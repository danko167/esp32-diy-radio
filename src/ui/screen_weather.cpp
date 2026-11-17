// Weather (design/src/screens/Weather.tsx): the forecast at a glance on one
// screen, with Hourly, Next days and Details pages. Settings > Weather
// location is in screen_place.cpp.

#include <Arduino.h>
#include <math.h>

#include "app/settings.h"
#include "hal/pins.h"
#include "net/weather.h"
#include "net/wifi.h"
#include "screens.h"
#include "theme.h"

namespace ui::screens {
namespace {

constexpr uint32_t kWarm = theme::kOrange;  // the hot end of the day ranges
constexpr int kGlanceStep = 3;              // hours between glance and chart points
constexpr int kChartPoints = 8;
// The chart card's content: the screen less the body's and the card's padding
constexpr int32_t kChartWidth = LCD_WIDTH - 2 * 16 - 2 * 18;
constexpr int32_t kChartHeight = 90;

lv_obj_t *weatherBody;
uint32_t shownRevision;
bool shownOnline;

lv_obj_t *flex(lv_obj_t *parent, lv_flex_flow_t flow, int32_t gap) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(obj, flow);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(obj, gap, 0);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_clickable(obj, false);
    return obj;
}

// A cell of a row split into equal columns
lv_obj_t *cell(lv_obj_t *row, int32_t gap) {
    lv_obj_t *c = flex(row, LV_FLEX_FLOW_COLUMN, gap);
    lv_obj_set_width(c, 0);
    lv_obj_set_flex_grow(c, 1);
    return c;
}

void formatHour(char *out, size_t size, const weather::Hour &h, bool first) {
    if (first) {
        snprintf(out, size, "Now");
        return;
    }
    struct tm t;
    localtime_r(&h.time, &t);
    snprintf(out, size, "%02d", t.tm_hour);
}

// "Plzeň" to "PLZEŇ", for captions: ASCII, Latin-1 and Latin Extended-A (what the fonts have)
String upper(const char *s) {
    String out;
    for (const uint8_t *p = reinterpret_cast<const uint8_t *>(s); *p;) {
        if (*p < 0x80) {
            out += (char)toupper(*p++);
            continue;
        }
        if ((p[0] & 0xE0) != 0xC0 || !p[1]) {
            out += (char)*p++;  // longer sequences pass through
            continue;
        }
        uint32_t c = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        p += 2;
        if (c >= 0xE0 && c <= 0xFE && c != 0xF7) {
            c -= 0x20;
        } else if (c == 0xFF) {
            c = 0x178;
        } else if ((c >= 0x100 && c <= 0x137) || (c >= 0x14A && c <= 0x177)) {
            c &= ~1u;  // pairs: upper case even, lower case odd
        } else if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) {
            if (c % 2 == 0) c -= 1;  // pairs: upper case odd
        }
        out += (char)(0xC0 | (c >> 6));
        out += (char)(0x80 | (c & 0x3F));
    }
    return out;
}

// "WEATHER · ZAGREB" on the pages
String pageCaption() {
    weather::Place p;
    return weather::place(p) ? "WEATHER · " + upper(p.name) : String("WEATHER");
}

// A rounded list with thin lines between the rows
lv_obj_t *list(lv_obj_t *parent) {
    lv_obj_t *l = addColumn(parent, 0);
    lv_obj_set_style_bg_color(l, lv_color_hex(theme::kSurface), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(l, 20, 0);
    lv_obj_set_style_pad_ver(l, 4, 0);
    lv_obj_set_style_pad_hor(l, 18, 0);
    return l;
}

lv_obj_t *listRow(lv_obj_t *list, int32_t height, bool last) {
    lv_obj_t *row = addRow(list, 12);
    lv_obj_set_height(row, height);
    if (!last) {
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(theme::kSurfaceHi), 0);
    }
    return row;
}

// Fixed width, so the columns of a list line up
lv_obj_t *column(lv_obj_t *row, const char *s, const lv_font_t *font, uint32_t colour, int32_t width,
                 lv_text_align_t align) {
    lv_obj_t *l = addText(row, s, font, colour);
    lv_obj_set_width(l, width);
    lv_obj_set_style_text_align(l, align, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    return l;
}

// ----- Hourly -----

lv_point_precise_t chartLine[kChartPoints];

void buildChart(lv_obj_t *card, const weather::Forecast &f) {
    const int n = min(kChartPoints, (f.hourCount + kGlanceStep - 1) / kGlanceStep);
    if (n == 0) return;
    char buf[16];
    float lo = 1000, hi = -1000;
    for (int i = 0; i < n; i++) {
        lo = min(lo, f.hours[i * kGlanceStep].temp);
        hi = max(hi, f.hours[i * kGlanceStep].temp);
    }

    lv_obj_t *icons = addRow(card, 0);
    for (int i = 0; i < n; i++) {
        const weather::Hour &h = f.hours[i * kGlanceStep];
        addText(cell(icons, 0), weather::icon(h.code, h.isDay), &font_weather_30, theme::kText);
    }

    // The curve: a line through a dot per point, the temperature above each
    lv_obj_t *chart = lv_obj_create(card);
    lv_obj_remove_style_all(chart);
    lv_obj_set_size(chart, kChartWidth, kChartHeight);
    lv_obj_set_scrollable(chart, false);
    lv_obj_set_clickable(chart, false);
    for (int i = 0; i < n; i++) {
        const float t = f.hours[i * kGlanceStep].temp;
        chartLine[i].x = (int32_t)((i + 0.5f) * kChartWidth / n);
        chartLine[i].y = (int32_t)(kChartHeight - 14 - (hi > lo ? (t - lo) / (hi - lo) : 0.5f) * (kChartHeight - 44));
    }
    lv_obj_t *line = lv_line_create(chart);
    lv_line_set_points(line, chartLine, n);
    lv_obj_set_style_line_width(line, 3, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(theme::accent()), 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    for (int i = 0; i < n; i++) {
        lv_obj_t *dot = addIconCircle(chart, "", 10, theme::accent(), LV_OPA_COVER, theme::accent());
        lv_obj_set_pos(dot, (int32_t)chartLine[i].x - 5, (int32_t)chartLine[i].y - 5);
        formatTemp(buf, sizeof(buf), f.hours[i * kGlanceStep].temp);
        lv_obj_t *label = addText(chart, buf, &font_ui_22, theme::kText);
        lv_obj_set_width(label, kChartWidth / n);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(label, (int32_t)chartLine[i].x - kChartWidth / n / 2, (int32_t)chartLine[i].y - 34);
    }

    lv_obj_t *rain = addRow(card, 0);
    lv_obj_t *hours = addRow(card, 0);
    for (int i = 0; i < n; i++) {
        const weather::Hour &h = f.hours[i * kGlanceStep];
        const bool wet = h.rainChance >= 10;
        snprintf(buf, sizeof(buf), wet ? "%u%%" : "–", h.rainChance);
        addText(cell(rain, 0), buf, &font_ui_20, wet ? theme::accent() : theme::kSurfaceHi);
        formatHour(buf, sizeof(buf), h, i == 0);
        addText(cell(hours, 0), buf, &font_ui_20, theme::kTextDim);
    }
}

void openHourly(lv_obj_t *screen) {
    weather::Forecast f;
    if (!weather::get(f)) return;
    const String caption = pageCaption();
    lv_obj_t *body = openSubpage(screen, "Next 24 hours", nullptr, caption.c_str());

    lv_obj_t *card = addCard(body, nullptr, true);
    lv_obj_set_style_pad_gap(card, 4, 0);
    buildChart(card, f);

    addSection(body, "HOUR BY HOUR");
    lv_obj_t *rows = list(body);
    char buf[16];
    for (int i = 0; i < f.hourCount; i++) {
        const weather::Hour &h = f.hours[i];
        lv_obj_t *row = listRow(rows, 52, i == f.hourCount - 1);
        if (i == 0) {
            snprintf(buf, sizeof(buf), "Now");
        } else {
            struct tm t;
            localtime_r(&h.time, &t);
            snprintf(buf, sizeof(buf), "%02d:00", t.tm_hour);
        }
        column(row, buf, &font_ui_22, theme::kTextDim, 72, LV_TEXT_ALIGN_LEFT);
        column(row, weather::icon(h.code, h.isDay), &font_weather_30, theme::kText, 40, LV_TEXT_ALIGN_CENTER);
        lv_obj_t *rain = flex(row, LV_FLEX_FLOW_ROW, 4);
        lv_obj_set_flex_grow(rain, 1);
        lv_obj_set_flex_align(rain, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        if (h.rainChance >= 10) {
            addText(rain, WI_RAINDROP, &font_weather_30, theme::accent());
            snprintf(buf, sizeof(buf), "%u%%", h.rainChance);
            addText(rain, buf, &font_ui_20, theme::accent());
        }
        formatTemp(buf, sizeof(buf), h.temp);
        addText(row, buf, &font_ui_30, theme::kText);
    }
}

// ----- Next days -----

void openDays(lv_obj_t *screen) {
    weather::Forecast f;
    if (!weather::get(f) || f.dayCount == 0) return;
    const String caption = pageCaption();
    lv_obj_t *body = openSubpage(screen, "Next days", nullptr, caption.c_str());

    // One scale for every bar, so the days compare at a glance
    float lo = 1000, hi = -1000;
    for (int i = 0; i < f.dayCount; i++) {
        lo = min(lo, f.days[i].min);
        hi = max(hi, f.days[i].max);
    }
    auto at = [&](float t) { return (int32_t)lroundf(constrain(hi > lo ? (t - lo) / (hi - lo) : 0.5f, 0, 1) * 100); };

    lv_obj_t *rows = list(body);
    char buf[24];
    for (int i = 0; i < f.dayCount; i++) {
        const weather::Day &d = f.days[i];
        lv_obj_t *row = listRow(rows, 62, i == f.dayCount - 1);

        lv_obj_t *name = flex(row, LV_FLEX_FLOW_COLUMN, 0);
        lv_obj_set_width(name, 140);
        lv_obj_set_flex_align(name, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        struct tm t;
        localtime_r(&d.date, &t);
        addText(name, i == 0 ? "Today" : dayName(t.tm_wday, false), &font_ui_22, theme::kText);
        const bool wet = d.rainChance >= 10;
        if (wet) snprintf(buf, sizeof(buf), "%u%% rain", d.rainChance);
        lv_obj_t *sub = addText(name, wet ? buf : weather::describe(d.code), &font_ui_20,
                             wet ? theme::accent() : theme::kTextDim);
        lv_obj_set_width(sub, 140);
        lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);

        column(row, weather::icon(d.code, true), &font_weather_30, theme::kText, 40, LV_TEXT_ALIGN_CENTER);
        formatTemp(buf, sizeof(buf), d.min);
        column(row, buf, &font_ui_22, theme::kTextDim, 36, LV_TEXT_ALIGN_RIGHT);

        // Track, the day's range on it, and a dot for now on today's
        lv_obj_t *track = lv_obj_create(row);
        lv_obj_remove_style_all(track);
        lv_obj_set_height(track, 8);
        lv_obj_set_width(track, 0);
        lv_obj_set_flex_grow(track, 1);
        lv_obj_set_style_radius(track, 4, 0);
        lv_obj_set_style_bg_color(track, lv_color_hex(theme::kSurfaceHi), 0);
        lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
        lv_obj_set_overflow_visible(track, true);  // the now dot sticks out
        lv_obj_set_scrollable(track, false);
        lv_obj_set_clickable(track, false);
        lv_obj_t *fill = lv_obj_create(track);
        lv_obj_remove_style_all(fill);
        lv_obj_set_pos(fill, LV_PCT(at(d.min)), 0);
        lv_obj_set_size(fill, LV_PCT(max<int32_t>(at(d.max) - at(d.min), 3)), LV_PCT(100));
        lv_obj_set_style_radius(fill, 4, 0);
        lv_obj_set_style_bg_color(fill, lv_color_hex(theme::accent()), 0);
        lv_obj_set_style_bg_grad_color(fill, lv_color_hex(kWarm), 0);
        lv_obj_set_style_bg_grad_dir(fill, LV_GRAD_DIR_HOR, 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
        lv_obj_set_clickable(fill, false);
        if (i == 0) {
            lv_obj_t *now = addIconCircle(track, "", 16, theme::kText, LV_OPA_COVER, theme::kText);
            lv_obj_set_style_border_width(now, 3, 0);
            lv_obj_set_style_border_color(now, lv_color_hex(theme::kSurface), 0);
            lv_obj_set_pos(now, LV_PCT(at(f.now.temp)), -4);
            lv_obj_set_style_translate_x(now, -8, 0);
        }

        formatTemp(buf, sizeof(buf), d.max);
        column(row, buf, &font_ui_22, theme::kText, 36, LV_TEXT_ALIGN_LEFT);
    }
}

// ----- Details -----

void openDetails(lv_obj_t *screen) {
    weather::Forecast f;
    if (!weather::get(f)) return;
    const String caption = pageCaption();
    lv_obj_t *body = openSubpage(screen, "Details", nullptr, caption.c_str());

    char values[6][24];
    formatTemp(values[0], sizeof(values[0]), f.now.feelsLike);
    snprintf(values[1], sizeof(values[1]), "%u%%", f.now.humidity);
    snprintf(values[2], sizeof(values[2]), "%ld km/h", lroundf(f.now.wind));
    const bool today = f.dayCount > 0;
    snprintf(values[3], sizeof(values[3]), today ? "%u%%" : "–", today ? f.days[0].rainChance : 0);
    snprintf(values[4], sizeof(values[4]), "–");
    snprintf(values[5], sizeof(values[5]), "–");
    if (today) {
        formatClock(values[4], sizeof(values[4]), f.days[0].sunrise);
        formatClock(values[5], sizeof(values[5]), f.days[0].sunset);
    }
    const char *const icons[] = {WI_THERMOMETER, WI_HUMIDITY, WI_WIND, WI_RAINDROP, WI_SUNRISE, WI_SUNSET};
    const char *const labels[] = {"Feels like", "Humidity", "Wind", "Rain today", "Sunrise", "Sunset"};

    lv_obj_t *grid = lv_obj_create(body);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_gap(grid, 10, 0);
    lv_obj_set_scrollable(grid, false);
    setGrid(grid, 2, 3);
    for (int i = 0; i < 6; i++) {
        // Icon on the left, value and label beside it, as on the Settings tiles
        lv_obj_t *tile = addRow(grid, 14);
        lv_obj_set_grid_cell(tile, LV_GRID_ALIGN_STRETCH, i % 2, 1, LV_GRID_ALIGN_START, i / 2, 1);
        lv_obj_set_height(tile, 102);
        lv_obj_set_style_pad_ver(tile, 10, 0);
        lv_obj_set_style_pad_hor(tile, 14, 0);
        lv_obj_set_style_radius(tile, 20, 0);
        lv_obj_set_style_bg_color(tile, lv_color_hex(theme::kSurface), 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        // Fixed width, so the values line up from tile to tile
        column(tile, icons[i], &font_weather_30, theme::accent(), 40, LV_TEXT_ALIGN_CENTER);
        lv_obj_t *text = addColumn(tile, 2);
        lv_obj_set_width(text, 0);
        lv_obj_set_flex_grow(text, 1);
        addText(text, values[i], &font_ui_30, theme::kText);
        addText(text, labels[i], &font_ui_20, theme::kTextDim);
    }

    // Open-Meteo data is CC BY 4.0, which asks for attribution
    char updated[12], buf[80];
    formatClock(updated, sizeof(updated), f.updated);
    snprintf(buf, sizeof(buf), "Updated %s · Weather data by Open-Meteo.com", updated);
    addHint(body, buf);
}

// ----- Now -----

void buildWaiting(lv_obj_t *body, bool online) {
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, 12, 0);
    if (online) {
        lv_obj_t *spinner = lv_spinner_create(body);
        lv_obj_set_size(spinner, 58, 58);
        lv_obj_set_style_margin_bottom(spinner, 8, 0);
        addText(body, "Getting the forecast…", &font_ui_22, theme::kText);
    } else {
        lv_obj_t *icon = addText(body, LV_SYMBOL_WIFI, &font_ui_50, theme::accent());
        lv_obj_set_style_margin_bottom(icon, 8, 0);
        addText(body, "No Wi-Fi, so no forecast.", &font_ui_22, theme::kText);
        addText(body, "Connect to Wi-Fi in Settings.", &font_ui_20, theme::kTextDim);
    }
}

void buildNow(lv_obj_t *body, const weather::Forecast &f) {
    char buf[64];
    // Fits the screen: nothing scrolls
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_bottom(body, 16, 0);

    lv_obj_t *hero = flex(body, LV_FLEX_FLOW_ROW, 24);
    addText(hero, weather::icon(f.now.code, f.now.isDay), &font_weather_120, theme::kText);
    lv_obj_t *heroText = flex(hero, LV_FLEX_FLOW_COLUMN, 2);
    lv_obj_set_flex_align(heroText, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    formatTemp(buf, sizeof(buf), f.now.temp);
    addText(heroText, buf, &font_clock_96, theme::kText);
    addText(heroText, weather::describe(f.now.code), &font_ui_30, theme::kText);
    char feels[12];
    formatTemp(feels, sizeof(feels), f.now.feelsLike);
    if (f.dayCount > 0) {
        char lo[12], hi[12];
        formatTemp(lo, sizeof(lo), f.days[0].min);
        formatTemp(hi, sizeof(hi), f.days[0].max);
        snprintf(buf, sizeof(buf), "Feels %s · today %s – %s", feels, lo, hi);
    } else {
        snprintf(buf, sizeof(buf), "Feels %s", feels);
    }
    addText(heroText, buf, &font_ui_22, theme::kTextDim);

    // Every third hour, a day ahead; tap for more
    lv_obj_t *glance = makeTappable(addCard(body, nullptr, false), [](lv_event_t *e) {
        openHourly(lv_obj_get_screen(lv_event_get_target_obj(e)));
    });
    // No gaps: the card's 16 px between eight columns left "Now" too little room
    lv_obj_set_style_pad_column(glance, 0, 0);
    for (int i = 0; i < f.hourCount && i / kGlanceStep < kChartPoints; i += kGlanceStep) {
        const weather::Hour &h = f.hours[i];
        lv_obj_t *c = cell(glance, 4);
        formatHour(buf, sizeof(buf), h, i == 0);
        addText(c, buf, &font_ui_20, theme::kTextDim);
        addText(c, weather::icon(h.code, h.isDay), &font_weather_30, theme::kText);
        formatTemp(buf, sizeof(buf), h.temp);
        addText(c, buf, &font_ui_22, theme::kText);
    }

    lv_obj_t *pills = addRow(body, 8);
    const struct {
        const char *label;
        void (*open)(lv_obj_t *);
    } kPills[] = {
        {SYM_CLOCK "  Hourly", openHourly},
        {LV_SYMBOL_LIST "  Days", openDays},
        {SYM_INFO "  Details", openDetails},
    };
    for (const auto &p : kPills) {
        lv_obj_t *pill = addPill(pills, p.label, false, 58, nullptr);
        lv_obj_add_event_cb(
            pill,
            [](lv_event_t *e) {
                auto open = reinterpret_cast<void (*)(lv_obj_t *)>(lv_event_get_user_data(e));
                open(lv_obj_get_screen(lv_event_get_target_obj(e)));
            },
            LV_EVENT_CLICKED, reinterpret_cast<void *>(p.open));
        lv_obj_set_width(pill, 0);
        lv_obj_set_flex_grow(pill, 1);
        lv_obj_set_style_pad_hor(pill, 0, 0);
    }
}

void rebuild() {
    lv_obj_clean(weatherBody);
    shownRevision = weather::revision();
    shownOnline = wifi::status() == wifi::Status::Connected;

    weather::Forecast f;
    weather::Place p;
    const bool havePlace = weather::place(p);
    const bool haveForecast = weather::get(f);
    lv_obj_set_scrollable(weatherBody, false);
    if (haveForecast && havePlace) {
        setAppTitle(weatherBody, p.name, "WEATHER");
    } else {
        setAppTitle(weatherBody, "Weather");
    }
    if (haveForecast) {
        buildNow(weatherBody, f);
    } else {
        buildWaiting(weatherBody, shownOnline);
    }
}

}  // namespace

lv_obj_t *createWeather() {
    weatherBody = createAppScreen("Weather");
    lv_obj_set_flex_flow(weatherBody, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(weatherBody, 10, 0);
    rebuild();
    every(weatherBody, 1000, [](lv_timer_t *) {
        const bool online = wifi::status() == wifi::Status::Connected;
        if (weather::revision() != shownRevision || online != shownOnline) rebuild();
    });
    return lv_obj_get_screen(weatherBody);
}

}  // namespace ui::screens
