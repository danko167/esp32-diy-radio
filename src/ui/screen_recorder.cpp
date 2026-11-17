// Recorder: Ready, Recording and the list of recordings (design/src/screens/Recorder.tsx)

#include <Arduino.h>
#include <time.h>

#include <vector>

#include "app/settings.h"
#include "audio/player.h"
#include "audio/recorder.h"
#include "hal/pins.h"
#include "screens.h"
#include "theme.h"

namespace ui::screens {
namespace {

constexpr int32_t kMeterWidth = LCD_WIDTH - 32;  // the body's width

lv_obj_t *body;
lv_obj_t *listBody;  // the Recordings page, while it's open
recorder::Quality quality = recorder::Quality::Voice;  // kept till the radio restarts

// What's shown, to know when to build it again
bool shownRecording = false;
uint32_t shownRevision = UINT32_MAX;
String shownPlaying;

// While recording
lv_obj_t *timeLabel, *meterFill, *detailLabel;

// The recording that plays, and since when (the player doesn't say how far it is)
String playingName;
uint32_t playingSince = 0;
uint32_t playingSeconds = 1;
std::vector<lv_obj_t *> progressBars;  // of the one playing, on each view that shows it

void rebuild();
// From the events of what rebuild() deletes
void rebuildSoon() {
    lv_async_call([](void *) { if (body) rebuild(); }, nullptr);
}

// "2:14", or "1:02:09" from an hour
String duration(uint32_t s) {
    char text[16];
    if (s >= 3600) {
        snprintf(text, sizeof(text), "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60),
                 (unsigned long)(s % 60));
    } else {
        snprintf(text, sizeof(text), "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    }
    return text;
}

// "4.1 MB", "820 KB"
String size(uint64_t bytes) {
    char text[16];
    if (bytes >= 1024ull * 1024 * 1024) {
        snprintf(text, sizeof(text), "%.1f GB", bytes / (1024.0 * 1024 * 1024));
    } else if (bytes >= 1024 * 1024) {
        snprintf(text, sizeof(text), "%.1f MB", bytes / (1024.0 * 1024));
    } else {
        snprintf(text, sizeof(text), "%lu KB", (unsigned long)((bytes + 512) / 1024));
    }
    return text;
}

// "Tue 3 Nov, 15:42", from the name (20261103-154412-voice.wav)
String title(const recorder::Recording &r) {
    static const char *const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    struct tm t = {};
    auto num = [&](int from, int count) { return (int)r.name.substring(from, from + count).toInt(); };
    t.tm_year = num(0, 4) - 1900;
    t.tm_mon = num(4, 2) - 1;
    t.tm_mday = num(6, 2);
    t.tm_hour = num(9, 2);
    t.tm_min = num(11, 2);
    t.tm_isdst = -1;
    mktime(&t);  // for the day of the week
    char clock[12];
    formatTime(clock, sizeof(clock), t);
    char text[40];
    snprintf(text, sizeof(text), "%s %d %s, %s", dayName(t.tm_wday, true), t.tm_mday,
             kMonths[t.tm_mon >= 0 && t.tm_mon < 12 ? t.tm_mon : 0], clock);
    return text;
}

// The name of the recording the player has, if it has one
String playingNow() {
    const player::Status s = player::status();
    if (s.state != player::State::Playing && s.state != player::State::Connecting) return "";
    const String source = s.source;
    const String folder = recorder::path("");
    return source.startsWith(folder) ? source.substring(folder.length()) : "";
}

lv_obj_t *plainBox(lv_obj_t *parent, int32_t w, int32_t h, uint32_t colour, int32_t radius) {
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

lv_obj_t *progressBar(lv_obj_t *parent) {
    lv_obj_t *track = plainBox(parent, LV_PCT(100), 6, theme::kSurfaceHi, 3);
    lv_obj_t *fill = plainBox(track, 0, LV_PCT(100), theme::accent(), 0);
    progressBars.push_back(fill);
    return track;
}

void showProgress() {
    if (progressBars.empty()) return;
    const uint32_t played = (millis() - playingSince) / 1000;
    const int32_t percent = std::min<uint32_t>(played * 100 / playingSeconds, 100);
    for (lv_obj_t *fill : progressBars) {
        if (lv_obj_get_style_width(fill, LV_PART_MAIN) != LV_PCT(percent)) lv_obj_set_width(fill, LV_PCT(percent));
    }
}

// ----- The list (Ready's latest ones, and the Recordings page) -----

void onPlay(lv_event_t *e) {
    auto *r = static_cast<recorder::Recording *>(lv_event_get_user_data(e));
    if (playingNow() == r->name) {
        player::stop();
        playingName = "";
        return;
    }
    playingName = r->name;
    playingSince = millis();
    playingSeconds = std::max<uint32_t>(r->seconds, 1);
    player::playFile(recorder::path(r->name).c_str(), false, true);
}

void onDelete(lv_event_t *e) {
    auto *r = static_cast<recorder::Recording *>(lv_event_get_user_data(e));
    if (playingNow() == r->name) player::stop();
    recorder::remove(r->name);  // the revision rebuilds the views
}

void addRecordings(lv_obj_t *parent, const std::vector<recorder::Recording> &list, size_t count, bool deletable) {
    if (list.empty()) {
        addSection(parent, "Nothing recorded yet");
        return;
    }
    const String playing = playingNow();
    for (size_t i = 0; i < list.size() && i < count; i++) {
        // The events need it after this returns: freed with the card
        auto *r = new recorder::Recording(list[i]);
        lv_obj_t *card = addCard(parent, nullptr, true);
        lv_obj_set_style_pad_gap(card, 12, 0);
        lv_obj_add_event_cb(
            card, [](lv_event_t *e) { delete static_cast<recorder::Recording *>(lv_event_get_user_data(e)); },
            LV_EVENT_DELETE, r);
        lv_obj_t *row = addRow(card, 14);
        const bool on = playing == r->name;
        addRoundButton(row, 48, on ? LV_SYMBOL_STOP : LV_SYMBOL_PLAY, on, onPlay, r);
        lv_obj_t *text = addColumn(row, 2);
        lv_obj_set_width(text, 0);
        lv_obj_set_flex_grow(text, 1);
        lv_obj_t *name = addText(text, title(*r).c_str(), &font_ui_22, theme::kText);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, LV_PCT(100));
        addText(text, (duration(r->seconds) + " · " + size(r->bytes)).c_str(), &font_ui_20, theme::kTextDim);
        if (deletable) addRoundButton(row, 44, LV_SYMBOL_TRASH, false, onDelete, r);
        if (on) progressBar(card);
    }
}

// Built again where it was scrolled to: a tap on play changes it
void keepScroll(lv_obj_t *obj, void (*build)()) {
    const int32_t y = lv_obj_get_scroll_y(obj);
    lv_obj_clean(obj);
    build();
    lv_obj_update_layout(obj);
    // From wherever cleaning left it, and no further than it now goes
    lv_obj_scroll_by_bounded(obj, 0, lv_obj_get_scroll_y(obj) - y, LV_ANIM_OFF);
}

void fillListPage() {
    if (!listBody) return;
    keepScroll(listBody, [] {
        addRecordings(listBody, recorder::list(), SIZE_MAX, true);
        addHint(listBody, "To download them to a computer, start the upload page in the Display settings, under Your picture.");
    });
}

void openList(lv_event_t *) {
    listBody = openSubpage(lv_obj_get_screen(body), "Recordings",
                           [] {
                               listBody = nullptr;
                               rebuild();  // its progress bars went with it
                           },
                           "RECORDER");
    lv_obj_set_style_pad_row(listBody, 10, 0);
    progressBars.clear();
    rebuildSoon();
}

// ----- Ready -----

void onRecord(lv_event_t *) {
    if (playingNow().length()) player::stop();
    recorder::start(quality);
    rebuildSoon();  // Recording, or why not
}

void addQuality(lv_obj_t *row, recorder::Quality q, const char *what) {
    const bool on = q == quality;
    lv_obj_t *b = lv_obj_create(row);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, 0, 72);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_radius(b, 20, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(theme::kSurface), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(theme::kSurfaceHi), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(theme::accent()), 0);
    lv_obj_set_style_border_opa(b, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(b, 2, 0);
    lv_obj_set_scrollable(b, false);
    lv_obj_set_clickable(b, true);
    addText(b, recorder::qualityName(q), &font_ui_22, on ? theme::accent() : theme::kText);
    addText(b, what, &font_ui_20, theme::kTextDim);
    lv_obj_add_event_cb(
        b,
        [](lv_event_t *e) {
            quality = static_cast<recorder::Quality>((intptr_t)lv_event_get_user_data(e));
            rebuildSoon();
        },
        LV_EVENT_CLICKED, (void *)(intptr_t)q);
}

void buildReady() {
    lv_obj_t *top = addColumn(body, 0);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(top, 6, 0);
    lv_obj_set_style_pad_bottom(top, 4, 0);
    lv_obj_t *button = lv_obj_create(top);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, 150, 150);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(button, 7, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(theme::kRed), 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(theme::kRed), 0);
    lv_obj_set_style_bg_opa(button, 56, 0);  // 22 %
    lv_obj_set_style_bg_opa(button, 110, LV_STATE_PRESSED);
    lv_obj_set_scrollable(button, false);
    lv_obj_set_clickable(button, true);
    lv_obj_add_event_cb(button, onRecord, LV_EVENT_CLICKED, nullptr);
    lv_obj_center(addText(button, SYM_MIC, &font_ui_50, theme::kRed));
    if (recorder::why()) {
        lv_obj_t *why = addHint(top, recorder::why());
        lv_obj_set_style_text_color(why, lv_color_hex(theme::kTextSoft), 0);
        lv_obj_set_style_text_align(why, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(why, 10, 0);
    }

    addSection(body, "QUALITY");
    lv_obj_t *row = addRow(body, 10);
    addQuality(row, recorder::Quality::Voice, "small files");
    addQuality(row, recorder::Quality::Music, "full sound");
    const uint64_t free = recorder::cardFree();
    String room;
    if (!free) {
        room = "no microSD card: recordings go there";
    } else {
        const uint64_t minutes = free / recorder::bytesPerSecond(quality) / 60;
        room = minutes >= 120 ? "room for about " + String((unsigned long)(minutes / 60)) + " hours on the card"
                              : "room for about " + String((unsigned long)minutes) + " minutes on the card";
    }
    addHint(body, (String(recorder::qualityDetail(quality)) + " · " + room).c_str());

    const std::vector<recorder::Recording> list = recorder::list();
    addSection(body, "LATEST");
    addRecordings(body, list, 2, false);
    if (!list.empty()) {
        // Under the latest ones, scrolling with them: as wide and big as the alarm editor's Save
        lv_obj_t *all = addRow(body, 0);
        lv_obj_t *pill = addPill(all, (String(LV_SYMBOL_LIST "  All recordings (") + list.size() + ")").c_str(),
                                 false, 58, openList);
        lv_obj_set_flex_grow(pill, 1);
    }
}

// ----- Recording -----

void showRecording() {
    setTextIfChanged(timeLabel, duration(recorder::seconds()).c_str());
    const int32_t width = kMeterWidth * recorder::level() / 100;
    if (lv_obj_get_width(meterFill) != width) lv_obj_set_width(meterFill, width);
    const recorder::Quality q = recorder::quality();
    setTextIfChanged(detailLabel, (String(recorder::qualityName(q)) + " · " + recorder::qualityDetail(q) + " · " +
                                   size(recorder::bytes()))
                                      .c_str());
}

void buildRecording() {
    lv_obj_t *live = addColumn(body, 14);
    lv_obj_set_flex_align(live, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(live, 6, 0);

    lv_obj_t *status = addRow(live, 10);
    lv_obj_set_width(status, LV_SIZE_CONTENT);
    lv_obj_t *dot = plainBox(status, 14, 14, theme::kRed, LV_RADIUS_CIRCLE);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, dot);
    lv_anim_set_values(&a, LV_OPA_COVER, 64);
    lv_anim_set_duration(&a, 700);
    lv_anim_set_reverse_duration(&a, 700);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, [](void *o, int32_t v) { lv_obj_set_style_bg_opa(static_cast<lv_obj_t *>(o), v, 0); });
    lv_anim_start(&a);
    addText(status, "Recording", &font_ui_22, theme::kRed);

    timeLabel = addText(live, "", &font_clock_96, theme::kText);

    // Green, then yellow from -18 dB, red from -7 dB: the colours stay put as it grows
    lv_obj_t *meter = plainBox(live, kMeterWidth, 14, theme::kSurfaceHi, 7);
    meterFill = plainBox(meter, 0, 14, theme::kGreen, 7);
    lv_obj_set_style_bg_opa(meterFill, LV_OPA_TRANSP, 0);
    lv_obj_set_style_clip_corner(meterFill, true, 0);
    const struct {
        int from, to;
        uint32_t colour;
    } zones[] = {{0, 70, theme::kGreen}, {70, 88, theme::kYellow}, {88, 100, theme::kRed}};
    for (const auto &z : zones) {
        lv_obj_t *part = plainBox(meterFill, kMeterWidth * (z.to - z.from) / 100, 14, z.colour, 0);
        lv_obj_set_x(part, kMeterWidth * z.from / 100);
    }

    detailLabel = addText(live, "", &font_ui_20, theme::kTextDim);

    lv_obj_t *stop = lv_obj_create(live);
    lv_obj_remove_style_all(stop);
    lv_obj_set_size(stop, 96, 96);
    lv_obj_set_style_margin_top(stop, 4, 0);
    lv_obj_set_style_radius(stop, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(stop, lv_color_hex(theme::kRed), 0);
    lv_obj_set_style_bg_opa(stop, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(stop, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_scrollable(stop, false);
    lv_obj_set_clickable(stop, true);
    lv_obj_add_event_cb(stop, [](lv_event_t *) { recorder::stop(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_center(plainBox(stop, 32, 32, theme::kText, 6));

    addHint(body, "The radio stopped for the recording; it doesn't start again by itself.");
    showRecording();
}

void rebuild() {
    // The other view starts at its top
    if (recorder::recording() != shownRecording) lv_obj_scroll_to_y(body, 0, LV_ANIM_OFF);
    shownRecording = recorder::recording();
    shownRevision = recorder::revision();
    shownPlaying = playingNow();
    if (shownPlaying != playingName) playingName = "";  // ended, or stopped elsewhere
    progressBars.clear();
    keepScroll(body, [] { shownRecording ? buildRecording() : buildReady(); });
    fillListPage();
    showProgress();
}

void tick(lv_timer_t *) {
    if (recorder::recording() != shownRecording) return rebuild();
    if (shownRecording) return showRecording();
    if (recorder::revision() != shownRevision || playingNow() != shownPlaying) return rebuild();
    showProgress();
}

}  // namespace

lv_obj_t *createRecorder() {
    body = createAppScreen("Recorder");
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 10, 0);
    listBody = nullptr;
    rebuild();
    every(body, 100, tick);
    // A recording playing is a preview: it ends with the screen (a recording doesn't)
    lv_obj_add_event_cb(
        body,
        [](lv_event_t *) {
            if (playingNow().length()) player::stop();
            body = listBody = nullptr;
            progressBars.clear();
        },
        LV_EVENT_DELETE, nullptr);
    return lv_obj_get_screen(body);
}

}  // namespace ui::screens
