#include <string.h>

#include "app/radio.h"
#include "app/settings.h"
#include "audio/recorder.h"
#include "net/weather.h"
#include "hal/pins.h"
#include "hal/power.h"
#include "net/upload_server.h"
#include "net/wifi.h"
#include "screens.h"
#include "theme.h"
#include "ui.h"
#include "wallpaper.h"

namespace ui::screens {
namespace {

constexpr uint32_t kLowBattery = theme::kRed;

// One row of tiles at the bottom, three to a page, that scrolls sideways a
// page at a time (design/src/screens/HomeV2.tsx). A page is the row's width:
// the gap between pages is twice the one between tiles.
constexpr int kPerPage = 3;
constexpr int kPages = (settings::kHomeTiles + kPerPage - 1) / kPerPage;
constexpr int32_t kTileWidth = 144;
constexpr int32_t kTileHeight = 134;
constexpr int32_t kTileGap = 12;
constexpr int32_t kRowBottom = 40;
constexpr int32_t kRowWidth = LCD_WIDTH;

// Indexed by App
const AppTile kTiles[] = {
    {SYM_TOWER, "FM Radio"},      {SYM_GLOBE, "Web Radio"}, {LV_SYMBOL_BLUETOOTH, "Bluetooth"},
    {LV_SYMBOL_TINT, "Weather"},   {LV_SYMBOL_BELL, "Alarm"},    {LV_SYMBOL_SETTINGS, "Settings"},
    {SYM_MIC, "Recorder"},         {SYM_RULER, "Level"},
};
static_assert(sizeof(kTiles) / sizeof(kTiles[0]) == settings::kHomeTiles, "a tile for each app");

lv_obj_t *background;
lv_obj_t *clockLabel;
lv_obj_t *dateLabel;
lv_obj_t *weatherIcon;
lv_obj_t *weatherTemp;
lv_obj_t *wifiIcon;
lv_obj_t *uploadIcon;  // while the upload server is on
// Where the upload icon would be: the radio is held off while recording or
// uploading, so only one of the three ever shows
lv_obj_t *recordingDot;  // red, while the Recorder records
lv_obj_t *playingDot;    // in the accent, while the radio plays
int shownPlaying = -1;   // -1: not shown yet, or the accent changed
lv_obj_t *batteryLabel;
lv_obj_t *alarmLabel;
lv_obj_t *tileRow;
lv_obj_t *dots[kPages];
App shownOrder[settings::kHomeTiles];
bool tilesBuilt = false;
int shownPage = 0;
uint32_t shownWeather = UINT32_MAX;
uint32_t version = 0;

// Frosted-glass tile: dark and translucent so the wallpaper shows through
lv_obj_t *createTile(lv_obj_t *parent, App app) {
    const AppTile &tile = appTile(app);
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, kTileWidth, kTileHeight);
    lv_obj_set_style_radius(obj, 28, 0);
    lv_obj_set_style_bg_color(obj, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_40, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_set_style_border_color(obj, lv_color_white(), 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_10, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_clickable(obj, true);
    // A finger that leaves it lets go: a swipe up from a tile (to close the
    // control centre, say) doesn't open it on lifting
    lv_obj_set_press_lock(obj, false);
    lv_obj_set_scrollable(obj, false);

    lv_obj_t *icon = lv_label_create(obj);
    lv_label_set_text(icon, tile.symbol);
    lv_obj_set_style_text_font(icon, &font_ui_50, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(theme::kText), 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -14);

    lv_obj_t *label = lv_label_create(obj);
    lv_label_set_text(label, tile.name);
    lv_obj_set_style_text_color(label, lv_color_hex(theme::kText), 0);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -17);

    // A drag that scrolls the row ends without a click, so it opens nothing
    lv_obj_add_event_cb(
        obj, [](lv_event_t *e) { ui::open(static_cast<App>((intptr_t)lv_event_get_user_data(e))); },
        LV_EVENT_CLICKED, (void *)(intptr_t)app);
    return obj;
}

// A dot in the status bar, left of Wi-Fi; tapping it opens `app`
lv_obj_t *statusDot(lv_obj_t *scr, uint32_t colour, App app) {
    lv_obj_t *dot = lv_obj_create(scr);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 14, 14);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(colour), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_align(dot, LV_ALIGN_TOP_RIGHT, -72, 22);
    lv_obj_set_hidden(dot, true);
    lv_obj_set_scrollable(dot, false);
    lv_obj_set_ext_click_area(dot, 20);
    lv_obj_add_event_cb(
        dot, [](lv_event_t *e) { ui::open(static_cast<App>((intptr_t)lv_event_get_user_data(e))); },
        LV_EVENT_CLICKED, (void *)(intptr_t)app);
    return dot;
}

// In the current order; only the first tile of each page is a snap point
void buildTiles(const App order[]) {
    lv_obj_clean(tileRow);
    for (int i = 0; i < settings::kHomeTiles; i++) {
        lv_obj_t *tile = createTile(tileRow, order[i]);
        if (i % kPerPage) {
            lv_obj_set_snappable(tile, false);
        } else if (i) {
            lv_obj_set_style_margin_left(tile, kTileGap, 0);
        }
        shownOrder[i] = order[i];
    }
    // A short last page: room that shows nothing, so it scrolls a whole page
    for (int i = settings::kHomeTiles; i < kPages * kPerPage; i++) {
        lv_obj_t *filler = lv_obj_create(tileRow);
        lv_obj_remove_style_all(filler);
        lv_obj_set_size(filler, kTileWidth, kTileHeight);
        lv_obj_set_snappable(filler, false);
        lv_obj_set_clickable(filler, false);
    }
    tilesBuilt = true;
}

void markPage() {
    for (int p = 0; p < kPages; p++) {
        lv_obj_set_style_bg_color(dots[p], p == shownPage ? lv_color_hex(theme::kText) : lv_color_white(), 0);
        lv_obj_set_style_bg_opa(dots[p], p == shownPage ? LV_OPA_COVER : LV_OPA_30, 0);
    }
}

int pageAtBegin = 0;

void onScrollBegin(lv_event_t *) { pageAtBegin = shownPage; }

// Where the snap animation is headed, not where it is now: the first
// SCROLL_END comes as the snap starts. A long fast swipe can head past the
// next page, or between two (scroll_one doesn't hold with three pages): it
// goes to the next page instead.
void onScrollEnd(lv_event_t *) {
    lv_point_t end;
    lv_obj_get_scroll_end(tileRow, &end);
    int page = LV_CLAMP(0, (end.x + kRowWidth / 2) / kRowWidth, kPages - 1);
    page = LV_CLAMP(pageAtBegin - 1, page, pageAtBegin + 1);
    const bool off = end.x != page * kRowWidth;
    if (off) {
        shownPage = page;  // before: scrolling there begins (and ends) again
        lv_obj_scroll_to_x(tileRow, page * kRowWidth, LV_ANIM_ON);
    }
    if (page == shownPage && !off) return;
    shownPage = page;
    markPage();
    version++;
}

}  // namespace

const AppTile &appTile(App app) {
    const int i = static_cast<int>(app);
    return kTiles[i >= 0 && i < settings::kHomeTiles ? i : 0];
}

void homeTileOrder(App out[settings::kHomeTiles]) {
    const uint8_t *saved = settings::get().homeTiles;
    bool seen[settings::kHomeTiles] = {};
    bool valid = true;
    for (int i = 0; i < settings::kHomeTiles && valid; i++) {
        valid = saved[i] < settings::kHomeTiles && !seen[saved[i]];
        if (valid) seen[saved[i]] = true;
    }
    for (int i = 0; i < settings::kHomeTiles; i++) out[i] = static_cast<App>(valid ? saved[i] : i);
}

lv_obj_t *createHome() {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(theme::kBg), 0);
    lv_obj_set_scrollable(scr, false);

    background = lv_image_create(scr);
    lv_obj_set_pos(background, 0, 0);

    // A short bar at the top edge: something to pull down (the control centre)
    lv_obj_t *pull = lv_obj_create(scr);
    lv_obj_remove_style_all(pull);
    lv_obj_set_size(pull, 56, 5);
    lv_obj_align(pull, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_set_style_radius(pull, 3, 0);
    lv_obj_set_style_bg_color(pull, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(pull, LV_OPA_40, 0);
    lv_obj_set_clickable(pull, false);

    // Placed by the glyphs, to match the design: digits from y 97 to 166
    clockLabel = lv_label_create(scr);
    lv_obj_set_style_text_font(clockLabel, &font_clock_96, 0);
    lv_obj_set_style_text_color(clockLabel, lv_color_hex(theme::kText), 0);
    lv_obj_align(clockLabel, LV_ALIGN_TOP_MID, 0, 97);

    // Date, then current weather when there is some. As tall as the weather
    // icon even without it, so the date doesn't move when it comes.
    lv_obj_t *info = lv_obj_create(scr);
    lv_obj_remove_style_all(info);
    lv_obj_set_size(info, LV_SIZE_CONTENT, 42);
    lv_obj_set_flex_flow(info, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(info, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(info, 8, 0);
    lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 175);
    dateLabel = lv_label_create(info);
    lv_obj_set_style_text_color(dateLabel, lv_color_hex(theme::kTextSoft), 0);
    weatherIcon = lv_label_create(info);
    lv_obj_set_style_text_font(weatherIcon, &font_weather_30, 0);
    lv_obj_set_style_text_color(weatherIcon, lv_color_hex(theme::kText), 0);
    lv_obj_set_style_pad_left(weatherIcon, 8, 0);
    weatherTemp = lv_label_create(info);
    lv_obj_set_style_text_color(weatherTemp, lv_color_hex(theme::kText), 0);

    // The next alarm on a line of its own
    alarmLabel = lv_label_create(scr);
    lv_obj_set_style_text_color(alarmLabel, lv_color_hex(theme::kText), 0);
    lv_label_set_text(alarmLabel, "");
    lv_obj_align(alarmLabel, LV_ALIGN_TOP_MID, 0, 224);
    lv_obj_set_hidden(alarmLabel, true);

    wifiIcon = lv_label_create(scr);
    lv_label_set_text(wifiIcon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(wifiIcon, lv_color_hex(theme::kText), 0);
    lv_obj_align(wifiIcon, LV_ALIGN_TOP_RIGHT, -24, 16);

    uploadIcon = lv_label_create(scr);
    lv_label_set_text(uploadIcon, LV_SYMBOL_UPLOAD);
    lv_obj_set_style_text_color(uploadIcon, lv_color_hex(theme::kText), 0);
    lv_obj_align(uploadIcon, LV_ALIGN_TOP_RIGHT, -68, 16);
    lv_obj_set_hidden(uploadIcon, true);
    lv_obj_set_clickable(uploadIcon, true);
    lv_obj_set_ext_click_area(uploadIcon, 16);
    lv_obj_add_event_cb(
        uploadIcon,
        [](lv_event_t *) {
            openUploadPageNext();
            ui::open(App::Settings);
        },
        LV_EVENT_CLICKED, nullptr);

    recordingDot = statusDot(scr, theme::kRed, App::Recorder);
    playingDot = statusDot(scr, theme::accent(), App::Internet);

    batteryLabel = lv_label_create(scr);
    lv_label_set_text(batteryLabel, "");
    lv_obj_align(batteryLabel, LV_ALIGN_TOP_LEFT, 24, 16);
    lv_obj_set_hidden(batteryLabel, true);

    // Only this row scrolls, and only to whole pages: no bounce at the ends,
    // and a swipe never skips a page
    tileRow = lv_obj_create(scr);
    lv_obj_remove_style_all(tileRow);
    lv_obj_set_size(tileRow, kRowWidth, kTileHeight);
    lv_obj_align(tileRow, LV_ALIGN_BOTTOM_LEFT, 0, -kRowBottom);
    lv_obj_set_flex_flow(tileRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_hor(tileRow, kTileGap, 0);
    lv_obj_set_style_pad_column(tileRow, kTileGap, 0);
    lv_obj_set_scroll_dir(tileRow, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(tileRow, LV_SCROLL_SNAP_START);
    lv_obj_set_scroll_one(tileRow, true);
    lv_obj_set_scroll_elastic(tileRow, false);
    lv_obj_set_scrollbar_mode(tileRow, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(tileRow, onScrollBegin, LV_EVENT_SCROLL_BEGIN, nullptr);
    lv_obj_add_event_cb(tileRow, onScrollEnd, LV_EVENT_SCROLL_END, nullptr);

    lv_obj_t *dotRow = lv_obj_create(scr);
    lv_obj_remove_style_all(dotRow);
    lv_obj_set_size(dotRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(dotRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(dotRow, 8, 0);
    lv_obj_align(dotRow, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_clickable(dotRow, false);
    lv_obj_set_scrollable(dotRow, false);
    for (lv_obj_t *&dot : dots) {
        dot = lv_obj_create(dotRow);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_clickable(dot, false);
    }
    markPage();

    refreshHome();
    return scr;
}

void refreshHome() {
    shownPlaying = -1;  // the accent may have changed
    const lv_image_dsc_t *img = wallpaper::image(wallpaper::current());
    if (img) {
        lv_image_set_src(background, img);
        lv_obj_invalidate(background);
    }
    lv_obj_set_hidden(background, img == nullptr);
    App order[settings::kHomeTiles];
    homeTileOrder(order);
    if (!tilesBuilt || memcmp(order, shownOrder, sizeof(order)) != 0) buildTiles(order);
    lv_label_set_text(clockLabel, "");  // force a redraw in the new format
    shownWeather = UINT32_MAX;
    version++;
    tickHome();
}

void tickHome() {
    bool changed = false;
    struct tm t;
    char text[32];
    if (localNow(t)) {
        formatTime(text, sizeof(text), t);
        changed |= setTextIfChanged(clockLabel, text);
        formatDate(text, sizeof(text), t);
        changed |= setTextIfChanged(dateLabel, text);
    } else {
        changed |= setTextIfChanged(clockLabel, "--:--");
        changed |= setTextIfChanged(dateLabel, "Clock not set");
    }
    char alarm[24] = "";
    const bool haveAlarm = nextAlarmText(alarm, sizeof(alarm));
    if (setTextIfChanged(alarmLabel, alarm)) {
        changed = true;
        lv_obj_set_hidden(alarmLabel, !haveAlarm);
    }

    if (weather::revision() != shownWeather) {
        shownWeather = weather::revision();
        changed = true;
        weather::Forecast f;
        const bool have = weather::get(f);
        if (have) {
            lv_label_set_text(weatherIcon, weather::icon(f.now.code, f.now.isDay));
            formatTemp(text, sizeof(text), f.now.temp);
            lv_label_set_text(weatherTemp, text);
        }
        lv_obj_set_hidden(weatherIcon, !have);
        lv_obj_set_hidden(weatherTemp, !have);
    }

    // Shown only once a network is set up and Wi-Fi is on; dim while it's not connected
    static int shownWifi = -1;
    const wifi::Status s = wifi::status();
    if (static_cast<int>(s) != shownWifi) {
        shownWifi = static_cast<int>(s);
        changed = true;
        lv_obj_set_hidden(wifiIcon, s == wifi::Status::NotConfigured || s == wifi::Status::Off);
        lv_obj_set_style_text_opa(wifiIcon, s == wifi::Status::Connected ? LV_OPA_COVER : LV_OPA_40,
                                  0);
    }

    static int shownUpload = -1;
    if ((int)upload_server::running() != shownUpload) {
        shownUpload = upload_server::running();
        changed = true;
        lv_obj_set_hidden(uploadIcon, !shownUpload);
    }

    static int shownRecording = -1;
    if ((int)recorder::recording() != shownRecording) {
        shownRecording = recorder::recording();
        changed = true;
        lv_obj_set_hidden(recordingDot, !shownRecording);
    }

    if ((int)radio::active() != shownPlaying) {
        shownPlaying = radio::active();
        changed = true;
        lv_obj_set_style_bg_color(playingDot, lv_color_hex(theme::accent()), 0);
        lv_obj_set_hidden(playingDot, !shownPlaying);
    }

    // Level and charging, only when there's a battery to show
    static uint32_t shownBattery = UINT32_MAX;
    if (power::revision() != shownBattery) {
        shownBattery = power::revision();
        changed = true;
        const power::Battery &b = power::battery();
        lv_obj_set_hidden(batteryLabel, !b.present || b.percent < 0);
        if (b.present && b.percent >= 0) {
            // On USB: the bolt while charging, the plug once full
            const char *source = b.charging ? LV_SYMBOL_CHARGE " " : b.usb ? LV_SYMBOL_USB " " : "";
            lv_label_set_text_fmt(batteryLabel, "%s%s %d%%", source, batteryIcon(b.percent), b.percent);
            // Never red on USB: a cell that's low but on the cable is on its way up
            const bool low = !b.usb && (b.percent <= 15 || b.level != power::Level::Ok);
            lv_obj_set_style_text_color(batteryLabel, lv_color_hex(low ? kLowBattery : theme::kText), 0);
        }
    }
    if (changed) version++;
}

uint32_t homeVersion() { return version; }

}  // namespace ui::screens
