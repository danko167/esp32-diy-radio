// Alarms: the list, the editor and the sound page (design/src/screens/Alarms.tsx)

#include <Arduino.h>
#include <math.h>

#include <vector>

#include "app/alarms.h"
#include "app/settings.h"
#include "audio/player.h"
#include "audio/tones.h"
#include "hal/rtc.h"
#include "net/stations.h"
#include "screens.h"
#include "theme.h"

namespace ui::screens {
namespace {

constexpr uint32_t kDayOff = 0x4A4A50;  // day letters of an alarm that skips that day
const uint8_t kSnoozeOptions[] = {5, 9, 10, 15, 20};
const char *const kSnoozeLabels[] = {"5", "9", "10", "15", "20"};
constexpr int kSnoozeCount = sizeof(kSnoozeOptions);

lv_obj_t *listBody;
lv_obj_t *shownBanner;  // the list's first child, while there is one
uint32_t shownRevision = UINT32_MAX;
long shownMinute = -1;
std::vector<alarms::Alarm> shown;

void formatAlarmTime(char *out, size_t size, uint8_t hour, uint8_t minute) {
    struct tm t = {};
    t.tm_hour = hour;
    t.tm_min = minute;
    formatTime(out, size, t);
}

// "in 7 h 20 min"
String untilText(time_t when, time_t now) {
    const long minutes = (when - now + 59) / 60;
    if (minutes < 60) return "in " + String(minutes) + " min";
    if (minutes < 48 * 60) return "in " + String(minutes / 60) + " h " + String(minutes % 60) + " min";
    return "in " + String(minutes / (24 * 60)) + " days";
}

// Calendar days from today to `when`
int daysAway(time_t when, time_t now) {
    struct tm a, b;
    localtime_r(&when, &a);
    localtime_r(&now, &b);
    a.tm_hour = b.tm_hour = 12;  // noon dodges daylight saving changes
    a.tm_min = b.tm_min = a.tm_sec = b.tm_sec = 0;
    a.tm_isdst = b.tm_isdst = -1;
    return (int)lround(difftime(mktime(&a), mktime(&b)) / 86400.0);
}

// "Today", "Tomorrow", "Wednesday"
String dayWord(time_t when, time_t now) {
    const int days = daysAway(when, now);
    if (days == 0) return "Today";
    if (days == 1) return "Tomorrow";
    struct tm t;
    localtime_r(&when, &t);
    return dayName(t.tm_wday, false);
}

// Section caption on the left, something small on the right
lv_obj_t *sectionRow(lv_obj_t *parent, const char *title) {
    lv_obj_t *row = addRow(parent, 0);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_right(row, 8, 0);
    addSection(row, title);
    return row;
}

// ----- Editor state -----

alarms::Alarm draft;
lv_obj_t *ringHint;
lv_obj_t *dayButtons[7];
lv_obj_t *repeatValue, *presetChips, *soundValue, *labelValue;

const uint8_t kPresetDays[] = {alarms::kWeekdays, alarms::kWeekend, alarms::kEveryDay};
const char *const kPresetLabels[] = {"Weekdays", "Weekends", "Every day"};

void showSoundValue() {
    String text = draft.soundName + " · " + String(draft.volume) + "%";
    lv_label_set_text(soundValue, text.c_str());
}

// ----- Sound page -----

std::vector<tones::Sound> sounds;
std::vector<lv_obj_t *> soundCards;
bool previewing = false;

// A preview repeats until stopped: when the Sound page closes, and when the
// whole screen goes (the middle button leaves without closing pages first)
void stopPreview() {
    if (!previewing) return;
    previewing = false;
    player::stop();
    player::setVolume(settings::get().volume);
}

void markSound() {
    for (size_t i = 0; i < sounds.size(); i++) {
        const bool selected = sounds[i].id == draft.sound;
        lv_obj_set_style_text_color(lv_obj_get_child(soundCards[i], 1),
                                    lv_color_hex(selected ? theme::accent() : theme::kText), 0);
        lv_obj_set_hidden(lv_obj_get_child(soundCards[i], 2), !selected);
    }
}

void openSoundPage(lv_obj_t *screen) {
    lv_obj_t *body = openSubpage(
        screen, "Sound",
        [] {
            stopPreview();
            showSoundValue();
        },
        draft.id ? "EDIT ALARM" : "NEW ALARM");

    addSection(body, "VOLUME");
    addBigSlider(body, 10, 100, draft.volume, LV_SYMBOL_VOLUME_MAX, [](lv_event_t *e) {
        draft.volume = lv_slider_get_value(lv_event_get_target_obj(e));
        if (previewing) player::setVolume(draft.volume);
    });
    lv_obj_t *fade = addCard(body, "Fade in over a minute", false);
    addSwitch(fade, draft.gentle, [](lv_event_t *e) {
        draft.gentle = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    });

    sounds = tones::list();
    for (const stations::Station &s : stations::favourites()) {
        sounds.push_back({"radio:" + s.url, s.name});
    }
    soundCards.assign(sounds.size(), nullptr);

    const struct {
        const char *prefix, *title, *icon;
    } kGroups[] = {
        {"tone:", "TONES", LV_SYMBOL_BELL},
        {"sd:", "FROM THE SD CARD", LV_SYMBOL_SD_CARD},
        {"radio:", "RADIO, YOUR FAVOURITES", LV_SYMBOL_WIFI},
    };
    for (const auto &g : kGroups) {
        bool first = true;
        for (size_t i = 0; i < sounds.size(); i++) {
            if (!sounds[i].id.startsWith(g.prefix)) continue;
            if (first) addSection(body, g.title);
            first = false;
            lv_obj_t *card = makeTappable(
                addCard(body, nullptr, false),
                [](lv_event_t *e) {
                    const size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
                    draft.sound = sounds[i].id;
                    draft.soundName = sounds[i].name;
                    markSound();
                    player::setVolume(draft.volume);
                    tones::play(draft.sound);  // hear it
                    previewing = true;
                },
                (void *)(intptr_t)i);
            lv_obj_set_style_pad_ver(card, 14, 0);
            lv_obj_set_style_pad_gap(card, 14, 0);
            lv_obj_t *icon = addText(card, g.icon, &font_ui_22, theme::kTextDim);
            lv_obj_set_width(icon, 30);  // icons differ in width; keep the names lined up
            lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_t *name = addText(card, sounds[i].name.c_str(), &font_ui_22, theme::kText);
            lv_obj_set_flex_grow(name, 1);
            lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
            addText(card, LV_SYMBOL_OK, &font_ui_22, theme::accent());
            soundCards[i] = card;
        }
    }
    markSound();
    addHint(body, "Tap a sound to hear it. If a station can't play, the chime rings instead.");
}

// ----- Editor -----

void showRingHint() {
    if (!rtc::timeValid()) {
        lv_obj_set_hidden(ringHint, true);
        return;
    }
    alarms::Alarm a = draft;
    a.enabled = true;
    const time_t now = time(nullptr);
    const time_t next = alarms::nextRing(a, now);
    String word = dayWord(next, now);
    if (daysAway(next, now) <= 1) {
        word.toLowerCase();  // "today", "tomorrow"
    } else {
        word = "on " + word;
    }
    String text = "Rings " + word + ", " + untilText(next, now);
    lv_label_set_text(ringHint, text.c_str());
}

void showDays() {
    for (int i = 0; i < 7; i++) {
        const bool on = draft.days & (1 << i);
        lv_obj_set_style_bg_color(dayButtons[i], lv_color_hex(on ? theme::accent() : theme::kSurfaceHi), 0);
        lv_obj_set_style_text_color(dayButtons[i], lv_color_hex(on ? theme::kBg : theme::kText), 0);
    }
    lv_label_set_text(repeatValue, alarms::describeDays(draft.days).c_str());
    int preset = -1;
    for (int i = 0; i < 3; i++) {
        if (draft.days == kPresetDays[i]) preset = i;
    }
    selectChip(presetChips, preset);
    showRingHint();
}

// "00\n01\n...", `count` of them
String rollerNumbers(int count) {
    String options;
    char buf[4];
    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "%02d", i);
        if (i) options += '\n';
        options += buf;
    }
    return options;
}

lv_obj_t *timeRoller(lv_obj_t *parent, const String &options, lv_roller_mode_t mode, int selected) {
    return addRoller(parent, options.c_str(), mode, selected, 130, &font_ui_50);
}

void openEditor(lv_obj_t *screen, const alarms::Alarm &a) {
    draft = a;
    lv_obj_t *body = openSubpage(screen, a.id ? "Edit alarm" : "New alarm", nullptr, "ALARMS");

    // Save stays put at the bottom
    lv_obj_t *footer = addFooter(body, 56);
    lv_obj_set_flex_flow(footer, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(footer, 10, 0);
    if (draft.id) {
        addRoundButton(footer, 58, LV_SYMBOL_TRASH, false, [](lv_event_t *e) {
            alarms::remove(draft.id);
            closeSubpage(lv_event_get_target_obj(e));
        });
    }
    lv_obj_t *save = addPill(footer, LV_SYMBOL_OK "  Save", true, 58, [](lv_event_t *e) {
        alarms::save(draft);
        closeSubpage(lv_event_get_target_obj(e));
    });
    lv_obj_set_flex_grow(save, 1);

    lv_obj_t *times = addRow(body, 12);
    lv_obj_set_flex_align(times, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *hours = timeRoller(times, rollerNumbers(24), LV_ROLLER_MODE_INFINITE, draft.hour);
    addText(times, ":", &font_ui_50, theme::kText);
    lv_obj_t *minutes = timeRoller(times, rollerNumbers(60), LV_ROLLER_MODE_INFINITE, draft.minute);
    lv_obj_add_event_cb(
        hours,
        [](lv_event_t *e) {
            const uint32_t selected = lv_roller_get_selected(lv_event_get_target_obj(e));
            draft.hour = selected;
            showRingHint();
        },
        LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(
        minutes,
        [](lv_event_t *e) {
            draft.minute = lv_roller_get_selected(lv_event_get_target_obj(e));
            showRingHint();
        },
        LV_EVENT_VALUE_CHANGED, nullptr);
    ringHint = addText(body, "", &font_ui_20, theme::kTextDim);
    lv_obj_set_width(ringHint, LV_PCT(100));
    lv_obj_set_style_text_align(ringHint, LV_TEXT_ALIGN_CENTER, 0);

    repeatValue = addText(sectionRow(body, "REPEAT"), "", &font_ui_22, theme::accent());
    lv_obj_t *days = addRow(body, 0);
    lv_obj_set_flex_align(days, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const char *const kDays[] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
    for (int i = 0; i < 7; i++) {
        // showDays() colours them
        dayButtons[i] = addRoundButton(
            days, 58, kDays[i], false,
            [](lv_event_t *e) {
                draft.days ^= 1 << (intptr_t)lv_event_get_user_data(e);
                showDays();
            },
            (void *)(intptr_t)i);
    }
    // Tapping the preset that's on goes back to ringing once
    presetChips = addChips(body, kPresetLabels, 3, 3, -1, true, 50, [](int i) {
        draft.days = draft.days == kPresetDays[i] ? 0 : kPresetDays[i];
        showDays();
    });
    showDays();

    lv_obj_t *card = makeTappable(addCard(body, "Sound", false), [](lv_event_t *e) {
        openSoundPage(lv_obj_get_screen(lv_event_get_target_obj(e)));
    });
    soundValue = addValue(card, "");
    showSoundValue();

    card = makeTappable(addCard(body, "Name", false), [](lv_event_t *e) {
        openTextInput(lv_obj_get_screen(lv_event_get_target_obj(e)), "Alarm name", false,
                      [](const char *name) {
                          draft.label = name;
                          lv_label_set_text(labelValue, draft.label.isEmpty() ? "None" : name);
                          lv_obj_set_style_text_color(
                              labelValue,
                              lv_color_hex(draft.label.isEmpty() ? theme::kTextDim : theme::accent()), 0);
                      });
    });
    labelValue = addValue(card, draft.label.isEmpty() ? "None" : draft.label.c_str());
    if (draft.label.isEmpty()) lv_obj_set_style_text_color(labelValue, lv_color_hex(theme::kTextDim), 0);

    addText(sectionRow(body, "SNOOZE"), "minutes", &font_ui_20, theme::kTextDim);
    int snooze = 1;
    for (int i = 0; i < kSnoozeCount; i++) {
        if (kSnoozeOptions[i] == draft.snoozeMinutes) snooze = i;
    }
    addChips(body, kSnoozeLabels, kSnoozeCount, 5, snooze, true, 50,
             [](int i) { draft.snoozeMinutes = kSnoozeOptions[i]; });
}

// ----- List -----

// What's coming up: the next alarm, a snoozed one, or none at all
lv_obj_t *addBanner(lv_obj_t *parent, time_t now) {
    const time_t snoozed = alarms::snoozedUntil();
    const time_t next = alarms::nextRing(now);

    lv_obj_t *banner = addRow(parent, 14);
    lv_obj_set_style_radius(banner, 20, 0);
    lv_obj_set_style_pad_all(banner, 12, 0);
    lv_obj_set_style_pad_right(banner, 16, 0);
    lv_obj_set_style_bg_opa(banner, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(banner, lv_color_hex(theme::kSurface), 0);
    if (snoozed || next) {
        // The accent at 12% over black
        const lv_color_t tint = lv_color_mix(lv_color_hex(theme::accent()), lv_color_black(), 31);
        lv_obj_set_style_bg_color(banner, tint, 0);
    }

    const bool on = snoozed || next;
    addIconCircle(banner, snoozed ? SYM_MOON : LV_SYMBOL_BELL, 50, on ? theme::accent() : theme::kSurfaceHi,
                  LV_OPA_COVER, on ? theme::kBg : theme::kTextDim);
    lv_obj_t *texts = addColumn(banner, 0);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_width(texts, 0);

    char clock[16];
    struct tm t;
    if (snoozed) {
        localtime_r(&snoozed, &t);
        formatAlarmTime(clock, sizeof(clock), t.tm_hour, t.tm_min);
        addText(texts, "SNOOZED", &font_ui_20, theme::accent());
        String line = String("Rings again at ") + clock;
        addText(texts, line.c_str(), &font_ui_22, theme::kText);
        addPill(banner, "Cancel", false, 46, [](lv_event_t *) { alarms::cancelSnooze(); });
    } else if (next) {
        localtime_r(&next, &t);
        formatAlarmTime(clock, sizeof(clock), t.tm_hour, t.tm_min);
        addText(texts, "NEXT ALARM", &font_ui_20, theme::accent());
        lv_obj_t *line = addRow(texts, 0);
        String when = dayWord(next, now) + " " + clock;
        addText(line, when.c_str(), &font_ui_22, theme::kText);
        String until = " · " + untilText(next, now);
        addText(line, until.c_str(), &font_ui_22, theme::kTextDim);
    } else {
        addText(texts, "No alarms on", &font_ui_22, theme::kTextDim);
    }
    return banner;
}

// The banner alone, each minute ("in 3 h 5 min") and when a switch flips: the
// rows stay, so the list keeps its place and a finger on it its press
void refreshBanner() {
    if (shownBanner) lv_obj_delete(shownBanner);
    shownBanner = nullptr;
    if (!rtc::timeValid() || shown.empty()) return;
    shownBanner = addBanner(listBody, time(nullptr));
    lv_obj_move_to_index(shownBanner, 0);
}

void openNew(lv_event_t *e) { openEditor(lv_obj_get_screen(lv_event_get_target_obj(e)), alarms::Alarm{}); }

// Keeps the scroll position: from wherever cleaning left it, and no further
// than the list now goes (as keepScroll() on the recorder screen)
void rebuild() {
    const int32_t scrollY = lv_obj_get_scroll_y(listBody);
    lv_obj_clean(listBody);
    shownBanner = nullptr;
    shown = alarms::list();
    const time_t now = time(nullptr);
    char buf[16];

    if (rtc::timeValid() && !shown.empty()) shownBanner = addBanner(listBody, now);

    for (size_t i = 0; i < shown.size(); i++) {
        const alarms::Alarm &a = shown[i];
        lv_obj_t *card = makeTappable(
            addCard(listBody, nullptr, false),
            [](lv_event_t *e) {
                const size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
                if (i < shown.size()) openEditor(lv_obj_get_screen(lv_event_get_target_obj(e)), shown[i]);
            },
            (void *)(intptr_t)i);

        lv_obj_t *col = addColumn(card, 0);
        lv_obj_set_flex_grow(col, 1);
        lv_obj_set_width(col, 0);
        formatAlarmTime(buf, sizeof(buf), a.hour, a.minute);
        addText(col, buf, &font_ui_50, a.enabled ? theme::kText : theme::kTextDim);

        // M T W T F S S, the days it rings lit up
        lv_obj_t *detail = addRow(col, 12);
        if (a.days) {
            lv_obj_t *letters = addRow(detail, 6);
            lv_obj_set_width(letters, LV_SIZE_CONTENT);
            const char *const kLetters[] = {"M", "T", "W", "T", "F", "S", "S"};
            for (int d = 0; d < 7; d++) {
                addText(letters, kLetters[d], &font_ui_20, a.days & (1 << d) ? theme::accent() : kDayOff);
            }
        } else {
            addText(detail, "Once", &font_ui_20, theme::kTextDim);
        }
        if (!a.label.isEmpty()) {
            lv_obj_t *label = addText(detail, a.label.c_str(), &font_ui_20, theme::kText);
            lv_obj_set_flex_grow(label, 1);
            lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        }

        // Changes its row in place: rebuilding would scroll the list and drop presses
        lv_obj_t *sw = addSwitch(card, a.enabled, [](lv_event_t *e) {
            lv_obj_t *sw = lv_event_get_target_obj(e);
            const size_t i = (size_t)(intptr_t)lv_obj_get_user_data(sw);
            if (i >= shown.size()) return;
            const bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
            const bool upToDate = alarms::revision() == shownRevision;
            alarms::setEnabled(shown[i].id, on);
            shown[i].enabled = on;
            if (upToDate) shownRevision = alarms::revision();  // else the tick rebuilds for the other change
            lv_obj_t *clock = lv_obj_get_child(lv_obj_get_child(lv_obj_get_parent(sw), 0), 0);
            lv_obj_set_style_text_color(clock, lv_color_hex(on ? theme::kText : theme::kTextDim), 0);
            refreshBanner();
        });
        lv_obj_set_user_data(sw, (void *)(intptr_t)i);
    }

    if (shown.empty()) {
        lv_obj_t *empty = addColumn(listBody, 16);
        lv_obj_set_flex_align(empty, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_top(empty, 40, 0);
        addText(empty, "No alarms yet.", &font_ui_22, theme::kTextDim);
        addPill(empty, LV_SYMBOL_PLUS "  Add an alarm", false, 58, openNew);
    }
    lv_obj_update_layout(listBody);
    lv_obj_scroll_by_bounded(listBody, 0, lv_obj_get_scroll_y(listBody) - scrollY, LV_ANIM_OFF);
}

}  // namespace

lv_obj_t *createAlarms() {
    listBody = createAppScreen("Alarms");
    lv_obj_set_flex_flow(listBody, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(listBody, 10, 0);
    addHeaderButton(listBody, LV_SYMBOL_PLUS, openNew);
    shownRevision = alarms::revision();
    shownMinute = time(nullptr) / 60;
    rebuild();

    every(listBody, 500, [](lv_timer_t *) {
        // Rebuild on changes; each minute only the banner, for the "in 3 h 5 min"
        const long minute = time(nullptr) / 60;
        if (alarms::revision() != shownRevision) {
            shownRevision = alarms::revision();
            shownMinute = minute;
            rebuild();
        } else if (minute != shownMinute) {
            shownMinute = minute;
            refreshBanner();
        }
    });
    lv_obj_add_event_cb(listBody, [](lv_event_t *) { stopPreview(); }, LV_EVENT_DELETE, nullptr);
    return lv_obj_get_screen(listBody);
}

}  // namespace ui::screens
