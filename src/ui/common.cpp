#include <math.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include "app/alarms.h"
#include "app/settings.h"
#include "hal/pins.h"
#include "hal/rtc.h"
#include "screens.h"
#include "theme.h"
#include "ui.h"

namespace ui::screens {
namespace {

constexpr int kHeaderHeight = 74;
constexpr int kHeaderButton = 54;

const char *const kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                             "Thursday", "Friday", "Saturday"};
const char *const kMonths[] = {"January", "February", "March", "April", "May", "June", "July",
                               "August", "September", "October", "November", "December"};

// Returns the title label
lv_obj_t *buildHeader(lv_obj_t *parent, const char *title, const char *caption, lv_event_cb_t onBack,
                      void *userData) {
    lv_obj_set_pos(addRoundButton(parent, kHeaderButton, LV_SYMBOL_LEFT, false, onBack, userData), 16, 10);

    // Always made (hidden without a caption), right before the title, so
    // setAppTitle() can find it
    lv_obj_t *small = lv_label_create(parent);
    lv_label_set_text(small, caption ? caption : "");
    lv_obj_set_hidden(small, !caption);
    lv_obj_set_style_text_font(small, &font_ui_20, 0);
    lv_obj_set_style_text_color(small, lv_color_hex(theme::accent()), 0);
    lv_obj_align(small, LV_ALIGN_TOP_LEFT, 86, 9);
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &font_ui_30, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(theme::kText), 0);
    lv_obj_set_width(label, LCD_WIDTH - 86 - 16 - kHeaderButton - 8);  // room for a header button
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 86, caption ? 33 : 21);
    return label;
}

lv_obj_t *buildBody(lv_obj_t *parent) {
    lv_obj_t *body = lv_obj_create(parent);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, LV_PCT(100), LCD_HEIGHT - kHeaderHeight);
    lv_obj_set_pos(body, 0, kHeaderHeight);
    lv_obj_set_style_pad_hor(body, 16, 0);
    lv_obj_set_style_pad_bottom(body, 24, 0);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    return body;
}

// Pages slide in and out as images (see ui::slideChange). A new page is built
// hidden by its caller after openSubpage() returns, so its slide starts on the
// next tick, once it's complete.
lv_obj_t *openingPage = nullptr;
lv_draw_buf_t *beforeOpening = nullptr;

void startOpening(void *) {
    if (!openingPage) return;
    lv_obj_set_hidden(openingPage, false);
    ui::slideChange(beforeOpening, true);
    openingPage = nullptr;
    beforeOpening = nullptr;
}

void closePage(lv_obj_t *page) {
    auto onClose = reinterpret_cast<void (*)()>(lv_obj_get_user_data(page));
    lv_obj_set_user_data(page, nullptr);
    if (onClose) onClose();
    lv_draw_buf_t *before = ui::snapshot(lv_obj_get_screen(page));
    lv_obj_set_hidden(page, true);
    lv_obj_delete_async(page);  // may be inside one of its own events
    ui::slideChange(before, false);
}

}  // namespace

lv_obj_t *createAppScreen(const char *title, const char *caption) {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(theme::kBg), 0);
    lv_obj_set_scrollable(scr, false);
    lv_obj_add_event_cb(
        scr,
        // Swipe either way to go back; the app leaves in the direction of the finger
        [](lv_event_t *) {
            const lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
            if (dir == LV_DIR_RIGHT) ui::back(ui::Exit::ToRight);
            if (dir == LV_DIR_LEFT) ui::back(ui::Exit::ToLeft);
        },
        LV_EVENT_GESTURE, nullptr);
    lv_obj_t *label = buildHeader(scr, title, caption, [](lv_event_t *) { ui::back(); }, nullptr);
    lv_obj_set_user_data(scr, label);  // for setAppTitle()
    return buildBody(scr);
}

void setAppTitle(lv_obj_t *body, const char *title, const char *caption) {
    lv_obj_t *label = static_cast<lv_obj_t *>(lv_obj_get_user_data(lv_obj_get_parent(body)));
    lv_obj_t *small = lv_obj_get_sibling(label, -1);
    setTextIfChanged(label, title);
    setTextIfChanged(small, caption ? caption : "");
    lv_obj_set_hidden(small, !caption);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 86, caption ? 33 : 21);
}

lv_obj_t *createPlaceholder(const char *title, const char *symbol, const char *text) {
    lv_obj_t *body = createAppScreen(title);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(body, 16, 0);

    lv_obj_t *icon = lv_label_create(body);
    lv_label_set_text(icon, symbol);
    lv_obj_set_style_text_font(icon, &font_ui_50, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(theme::accent()), 0);

    lv_obj_t *label = lv_label_create(body);
    lv_label_set_text(label, "Coming soon");
    lv_obj_set_style_text_font(label, &font_ui_30, 0);

    lv_obj_t *desc = lv_label_create(body);
    lv_label_set_text(desc, text);
    lv_obj_set_width(desc, 380);
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(desc, lv_color_hex(theme::kTextDim), 0);
    return lv_obj_get_screen(body);
}

lv_obj_t *openSubpage(lv_obj_t *screen, const char *title, void (*onClose)(), const char *caption) {
    lv_draw_buf_t *before = ui::snapshot(screen);
    lv_obj_t *page = lv_obj_create(screen);
    lv_obj_set_hidden(page, true);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, LCD_WIDTH, LCD_HEIGHT);
    lv_obj_set_style_bg_color(page, lv_color_hex(theme::kBg), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_clickable(page, true);  // keep taps away from what's underneath
    lv_obj_set_scrollable(page, false);
    lv_obj_set_gesture_bubble(page, false);
    lv_obj_set_user_data(page, reinterpret_cast<void *>(onClose));
    lv_obj_add_event_cb(
        page,
        [](lv_event_t *e) {
            if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT) {
                closePage(lv_event_get_current_target_obj(e));
            }
        },
        LV_EVENT_GESTURE, nullptr);
    buildHeader(
        page, title, caption,
        [](lv_event_t *e) { closePage(static_cast<lv_obj_t *>(lv_event_get_user_data(e))); },
        page);
    lv_obj_t *body = buildBody(page);
    lv_obj_set_gesture_bubble(body, true);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 10, 0);

    if (openingPage) startOpening(nullptr);  // two pages within one tick: no slide for the first
    openingPage = page;
    beforeOpening = before;
    lv_async_call(startOpening, nullptr);
    return body;
}

lv_obj_t *addHeaderButton(lv_obj_t *body, const char *symbol, lv_event_cb_t onTap) {
    lv_obj_t *btn = addRoundButton(lv_obj_get_parent(body), kHeaderButton, symbol, false, onTap);
    lv_obj_set_pos(btn, LCD_WIDTH - 16 - kHeaderButton, 10);
    return btn;
}

lv_obj_t *addPinnedTop(lv_obj_t *body, int32_t height) {
    lv_obj_t *area = lv_obj_create(lv_obj_get_parent(body));
    lv_obj_remove_style_all(area);
    lv_obj_set_size(area, LV_PCT(100), height);
    lv_obj_set_pos(area, 0, lv_obj_get_style_y(body, LV_PART_MAIN));
    lv_obj_set_style_pad_hor(area, 16, 0);
    lv_obj_set_scrollable(area, false);
    lv_obj_set_y(body, lv_obj_get_style_y(body, LV_PART_MAIN) + height);
    lv_obj_set_height(body, lv_obj_get_style_height(body, LV_PART_MAIN) - height);
    return area;
}

lv_obj_t *addFooter(lv_obj_t *body, int32_t height) {
    constexpr int32_t kMargin = 16;
    lv_obj_t *area = lv_obj_create(lv_obj_get_parent(body));
    lv_obj_remove_style_all(area);
    lv_obj_set_size(area, LV_PCT(100), height);
    lv_obj_set_pos(area, 0, LCD_HEIGHT - kMargin - height);
    lv_obj_set_style_pad_hor(area, 16, 0);
    lv_obj_set_scrollable(area, false);
    lv_obj_set_height(body, lv_obj_get_style_height(body, LV_PART_MAIN) - height - kMargin);
    lv_obj_set_style_pad_bottom(body, 8, 0);
    return area;
}

void closeSubpage(lv_obj_t *child) {
    // Walk up to the page: the direct child of the screen
    lv_obj_t *page = child;
    while (lv_obj_get_parent(page) && lv_obj_get_parent(page) != lv_obj_get_screen(child)) {
        page = lv_obj_get_parent(page);
    }
    closePage(page);
}

void addSection(lv_obj_t *parent, const char *title) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &font_ui_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(theme::accent()), 0);
    lv_obj_set_style_pad_top(label, 16, 0);
    lv_obj_set_style_pad_left(label, 8, 0);
}

lv_obj_t *addCard(lv_obj_t *parent, const char *title, bool vertical) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row, lv_color_hex(theme::kSurface), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 20, 0);
    lv_obj_set_style_pad_all(row, 20, 0);
    lv_obj_set_style_pad_gap(row, 16, 0);
    lv_obj_set_flex_flow(row, vertical ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(row, false);

    if (title) {
        lv_obj_t *label = lv_label_create(row);
        lv_label_set_text(label, title);
        lv_obj_set_style_text_color(label, lv_color_hex(theme::kText), 0);
    }
    return row;
}

lv_obj_t *addTitleCard(lv_obj_t *parent, const char *name, const lv_font_t *font, const char *detail) {
    lv_obj_t *card = addCard(parent, nullptr, true);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(card, 4, 0);
    addText(card, name, font, theme::kText);
    addText(card, detail, &font_ui_20, theme::kTextDim);
    return card;
}

lv_obj_t *makeTappable(lv_obj_t *card, lv_event_cb_t onTap, void *userData) {
    lv_obj_set_clickable(card, true);
    lv_obj_set_style_bg_color(card, lv_color_hex(theme::kSurfaceHi), LV_STATE_PRESSED);
    lv_obj_add_event_cb(card, onTap, LV_EVENT_CLICKED, userData);
    return card;
}

lv_obj_t *addValue(lv_obj_t *card, const char *text) {
    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(theme::accent()), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(label, 240, 0);
    return label;
}

bool localNow(struct tm &out) {
    if (!rtc::timeValid()) return false;
    time_t now = time(nullptr);
    localtime_r(&now, &out);
    return true;
}

void formatTime(char *out, size_t size, const struct tm &t) { snprintf(out, size, "%02d:%02d", t.tm_hour, t.tm_min); }

void formatClock(char *out, size_t size, time_t when) {
    struct tm t;
    localtime_r(&when, &t);
    formatTime(out, size, t);
}

void formatTemp(char *out, size_t size, float temp) { snprintf(out, size, "%ld°", lroundf(temp)); }

void formatDate(char *out, size_t size, const struct tm &t) {
    snprintf(out, size, "%s, %d %s", kDays[t.tm_wday], t.tm_mday, kMonths[t.tm_mon]);
}

const char *dayName(int wday, bool shortName) {
    static const char *const kShort[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    return shortName ? kShort[wday] : kDays[wday];
}

const char *batteryIcon(int percent) {
    return percent > 85   ? LV_SYMBOL_BATTERY_FULL
           : percent > 60 ? LV_SYMBOL_BATTERY_3
           : percent > 35 ? LV_SYMBOL_BATTERY_2
           : percent > 10 ? LV_SYMBOL_BATTERY_1
                          : LV_SYMBOL_BATTERY_EMPTY;
}

bool nextAlarmText(char *out, size_t size) {
    // The clocks ask 4 times a second, and nextRing() runs mktime() up to 8
    // times per alarm: work it out again only when the answer can change
    static char text[32];
    static bool have = false;
    static long forMinute = -1;
    static uint32_t forRevision = 0;
    if (!rtc::timeValid()) return false;
    const time_t now = time(nullptr);
    if (now / 60 != forMinute || alarms::revision() != forRevision) {
        forMinute = now / 60;
        forRevision = alarms::revision();
        const time_t next = alarms::nextRing(now);
        have = next != 0;
        if (have) {
            struct tm t;
            localtime_r(&next, &t);
            char clock[12];
            formatTime(clock, sizeof(clock), t);
            if (next - now < 24 * 3600) {
                snprintf(text, sizeof(text), LV_SYMBOL_BELL " %s", clock);
            } else {
                snprintf(text, sizeof(text), LV_SYMBOL_BELL " %s %s", dayName(t.tm_wday, true), clock);
            }
        }
    }
    if (have) strlcpy(out, text, size);
    return have;
}

bool setTextIfChanged(lv_obj_t *label, const char *text) {
    if (strcmp(lv_label_get_text(label), text) == 0) return false;
    lv_label_set_text(label, text);
    return true;
}

namespace {

// every()'s timers and whom they tick for. The timer's own user data stays
// free for the caller.
struct Ticker {
    lv_timer_t *timer;
    lv_obj_t *owner;
    lv_timer_cb_t tick;
};
std::vector<Ticker> tickers;

// App screens live on under the clock screen: their ticks wait until it's back
// (Wi-Fi's page scanned every 15 s all night). Also while a slide shows its
// images; the screen is active again when the slide ends.
void runTicker(lv_timer_t *timer) {
    for (const Ticker &t : tickers) {
        if (t.timer != timer) continue;
        const lv_timer_cb_t tick = t.tick;  // a tick may add or remove tickers
        if (lv_obj_get_screen(t.owner) == lv_screen_active()) tick(timer);
        return;
    }
}

}  // namespace

lv_timer_t *every(lv_obj_t *owner, uint32_t ms, lv_timer_cb_t tick) {
    lv_timer_t *timer = lv_timer_create(runTicker, ms, nullptr);
    tickers.push_back({timer, owner, tick});
    lv_obj_add_event_cb(
        owner,
        [](lv_event_t *e) {
            lv_timer_t *timer = static_cast<lv_timer_t *>(lv_event_get_user_data(e));
            for (size_t i = 0; i < tickers.size(); i++) {
                if (tickers[i].timer == timer) {
                    tickers.erase(tickers.begin() + i);
                    break;
                }
            }
            lv_timer_delete(timer);
        },
        LV_EVENT_DELETE, timer);
    return timer;
}

}  // namespace ui::screens
