// Settings > Date & time > Set by hand, for when there is no Wi-Fi to sync
// from (design/src/screens/Settings.tsx, SetByHand): a page like the alarm
// editor. The time in its rollers, the date in smaller ones under it, Set at
// the bottom; back cancels.

#include <Arduino.h>
#include <sys/time.h>

#include "hal/rtc.h"
#include "screens.h"
#include "theme.h"

namespace ui::screens {
namespace {

constexpr int kFirstYear = 2025;
constexpr int kYears = 16;

lv_obj_t *dayRoller, *monthRoller, *yearRoller, *hourRoller, *minuteRoller;

// "01\n02\n...": numbers from `first`, `count` of them
String numbers(int first, int count, bool pad) {
    String s;
    char buf[8];
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), pad ? "%02d" : "%d", first + i);
        if (i) s += '\n';
        s += buf;
    }
    return s;
}

int daysInMonth(int month0, int year) {
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month0 == 1 && leap ? 29 : kDays[month0];
}

// Rows closer than the alarm editor's, so the time and the date both fit
// above Set (the height follows the spacing, so the row count goes again)
lv_obj_t *tight(lv_obj_t *roller, int32_t lineSpace) {
    lv_obj_set_style_text_line_space(roller, lineSpace, 0);
    lv_roller_set_visible_row_count(roller, 3);
    return roller;
}

void save(lv_event_t *e) {
    struct tm t = {};
    t.tm_year = kFirstYear + lv_roller_get_selected(yearRoller) - 1900;
    t.tm_mon = lv_roller_get_selected(monthRoller);
    t.tm_mday = min<int>(lv_roller_get_selected(dayRoller) + 1, daysInMonth(t.tm_mon, t.tm_year + 1900));
    t.tm_hour = lv_roller_get_selected(hourRoller);
    t.tm_min = lv_roller_get_selected(minuteRoller);
    t.tm_isdst = -1;  // let the timezone rules decide
    timeval tv{mktime(&t), 0};
    settimeofday(&tv, nullptr);
    rtc::save();
    closeSubpage(lv_event_get_target_obj(e));  // Date & time shows it (onDone)
}

}  // namespace

void openTimeDialog(lv_obj_t *screen, void (*onDone)()) {
    struct tm now;
    if (!localNow(now)) {
        now = {};
        now.tm_year = kFirstYear - 1900;
        now.tm_mday = 1;
    }

    lv_obj_t *body = openSubpage(screen, "Set by hand", onDone, "DATE & TIME");
    lv_obj_t *footer = addFooter(body, 56);
    lv_obj_t *set = addPill(footer, LV_SYMBOL_OK "  Set", true, 58, save);
    lv_obj_set_width(set, LV_PCT(100));

    lv_obj_t *time = addRow(body, 12);
    lv_obj_set_flex_align(time, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    hourRoller = tight(
        addRoller(time, numbers(0, 24, true).c_str(), LV_ROLLER_MODE_INFINITE, now.tm_hour, 130, &font_ui_50), 2);
    addText(time, ":", &font_ui_50, theme::kText);
    minuteRoller = tight(
        addRoller(time, numbers(0, 60, true).c_str(), LV_ROLLER_MODE_INFINITE, now.tm_min, 130, &font_ui_50), 2);

    lv_obj_t *date = addRow(body, 10);
    lv_obj_set_flex_align(date, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_margin_top(date, 4, 0);
    dayRoller = tight(addRoller(date, numbers(1, 31, false).c_str(), LV_ROLLER_MODE_INFINITE, now.tm_mday - 1, 96,
                                &font_ui_30),
                      0);
    monthRoller = tight(addRoller(date, "Jan\nFeb\nMar\nApr\nMay\nJun\nJul\nAug\nSep\nOct\nNov\nDec",
                                  LV_ROLLER_MODE_INFINITE, now.tm_mon, 120, &font_ui_30),
                        0);
    yearRoller = tight(addRoller(date, numbers(kFirstYear, kYears, false).c_str(), LV_ROLLER_MODE_NORMAL,
                                 constrain(now.tm_year + 1900 - kFirstYear, 0, kYears - 1), 140, &font_ui_30),
                       0);
}

}  // namespace ui::screens
