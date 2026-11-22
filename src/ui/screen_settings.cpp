// Settings: six tiles that fit the screen, each opening its own page
// (design/src/screens/Settings.tsx). Wi-Fi and Weather location are in
// screen_wifi.cpp and screen_place.cpp.

#include <Arduino.h>

#include <SD_MMC.h>

#include <vector>

#include "app/backup.h"
#include "app/settings.h"
#include "app/storage.h"
#include "app/version.h"
#include "app/watchdog.h"
#include "hal/display.h"
#include "hal/light.h"
#include "hal/power.h"
#include "hal/rtc.h"
#include "net/weather.h"
#include "net/wifi.h"
#include "card_faces.h"
#include "screens.h"
#include "theme.h"
#include "ui.h"
#include "wallpaper.h"

namespace ui::screens {
namespace {

constexpr const char *kCaption = "SETTINGS";

// Seconds of inactivity before the clock screen, 0 = never. Non-breaking
// spaces keep "1 min" together where the Settings tile wraps its summary.
const uint16_t kTimeouts[] = {30, 60, 120, 300, 600, 0};
const char *const kTimeoutLabels[] = {"30\u00A0s",  "1\u00A0min",  "2\u00A0min",
                                      "5\u00A0min", "10\u00A0min", "Never"};
constexpr int kTimeoutCount = sizeof(kTimeouts) / sizeof(kTimeouts[0]);

// In the order of settings::AmbientMode
const char *const kModeLabels[] = {"Clock", "+ Weather", "Off"};
// In the order of settings::NightColour
const char *const kNightColourNames[] = {"Red", "Green"};

// The built-in face in use, or Card for one from the card
settings::ClockFace faceInUse() {
    const settings::ClockFace f = settings::get().clockFace;
    return f == settings::ClockFace::Card || static_cast<int>(f) < card_faces::kBuiltInCount
               ? f
               : settings::ClockFace::Digital;
}

// A card face's name; Digital's while its card is out, as the clock screen shows that then
const char *faceName() {
    if (faceInUse() != settings::ClockFace::Card) return card_faces::kBuiltIn[static_cast<int>(faceInUse())];
    const card_faces::Face *f = card_faces::find(settings::get().clockFaceFile);
    return f ? f->name.c_str() : card_faces::kBuiltIn[0];
}

// Night and Segment: dim, in red or green
bool isNightFace(settings::ClockFace face) {
    return face == settings::ClockFace::Night || face == settings::ClockFace::Segment;
}

bool faceShowsWeather() {
    return settings::get().ambientMode == settings::AmbientMode::ClockWeather;
}

enum Tile { kWifi, kDisplay, kClockScreen, kDateTime, kPlace, kAbout, kTileCount };
lv_obj_t *tileValues[kTileCount];

// Brightness is 0..255 on the panel, a percentage on screen
int toPercent(uint8_t level) { return (level * 100 + 127) / 255; }
uint8_t toLevel(int percent) { return (percent * 255 + 50) / 100; }

bool checked(lv_event_t *e) { return lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED); }

void clockText(char *out, size_t size, bool seconds) {
    struct tm t;
    if (!localNow(t)) {
        snprintf(out, size, "--:--");
        return;
    }
    formatTime(out, size, t);
    if (seconds) snprintf(out + strlen(out), size - strlen(out), ":%02d", t.tm_sec);
}

void refreshTiles() {
    if (!tileValues[0]) return;
    const auto &s = settings::get();
    char buf[64];

    const wifi::Status ws = wifi::status();
    setTextIfChanged(tileValues[kWifi], ws == wifi::Status::Connected    ? wifi::ssid().c_str()
                                        : ws == wifi::Status::Connecting ? "Connecting…"
                                        : ws == wifi::Status::Off        ? "Off"
                                                                         : "Not connected");

    snprintf(buf, sizeof(buf), "%d%% · %s", toPercent(s.brightness), wallpaper::name(wallpaper::current()));
    setTextIfChanged(tileValues[kDisplay], buf);

    int t = 0;
    while (t < kTimeoutCount - 1 && kTimeouts[t] != s.ambientTimeoutS) t++;
    if (s.ambientTimeoutS && s.ambientMode == settings::AmbientMode::ScreenOff) {
        snprintf(buf, sizeof(buf), "Screen off, %s", kTimeoutLabels[t]);
    } else if (s.ambientTimeoutS) {
        snprintf(buf, sizeof(buf), "%s%s, %s", faceName(), faceShowsWeather() ? " + weather" : "",
                 kTimeoutLabels[t]);
    } else {
        snprintf(buf, sizeof(buf), "Never");
    }
    setTextIfChanged(tileValues[kClockScreen], buf);

    char time[16];
    clockText(time, sizeof(time), false);
    setTextIfChanged(tileValues[kDateTime], time);

    weather::Place p;
    setTextIfChanged(tileValues[kPlace], weather::place(p) ? p.name : "Automatic");
    setTextIfChanged(tileValues[kAbout], "Memory, developer");
}

lv_obj_t *page(lv_obj_t *screen, const char *title) {
    return openSubpage(screen, title, refreshTiles, kCaption);
}

lv_obj_t *switchCard(lv_obj_t *body, const char *title, bool on, lv_event_cb_t onChange) {
    lv_obj_t *card = addCard(body, title, false);
    addSwitch(card, on, onChange);
    return card;
}

// Card with dim text on the right
lv_obj_t *infoCard(lv_obj_t *body, const char *title) {
    lv_obj_t *card = addCard(body, title, false);
    lv_obj_t *value = addText(card, "", &font_ui_20, theme::kTextDim);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_obj_set_style_max_width(value, 260, 0);
    return value;
}

// ----- Display -----

std::vector<lv_obj_t *> wallpaperPicks;

void markWallpaper() {
    for (size_t i = 0; i < wallpaperPicks.size(); i++) {
        const bool selected = (int)i == wallpaper::current();
        lv_obj_t *frame = lv_obj_get_child(wallpaperPicks[i], 0);
        lv_obj_set_style_border_color(frame, lv_color_hex(selected ? theme::accent() : theme::kSurfaceHi), 0);
        lv_obj_set_hidden(lv_obj_get_child(frame, 1), !selected);  // check badge
        lv_obj_set_style_text_color(lv_obj_get_child(wallpaperPicks[i], 1),
                                    lv_color_hex(selected ? theme::accent() : theme::kTextDim), 0);
    }
}

lv_obj_t *accentPicks[theme::kAccentCount];
lv_obj_t *accentHint;

void markAccent() {
    const int selected = theme::accentIndex();
    for (int i = 0; i < theme::kAccentCount; i++) {
        lv_obj_set_style_bg_color(accentPicks[i], lv_color_hex(theme::kAccents[i].colour), 0);
        lv_obj_set_style_outline_width(accentPicks[i], i == selected ? 3 : 0, 0);
        lv_obj_set_hidden(lv_obj_get_child(accentPicks[i], 0), i != selected);  // check mark
    }
    char text[96];
    snprintf(text, sizeof(text), "%s: for highlights, buttons and what's selected, throughout the radio.",
             theme::kAccents[selected].name);
    setTextIfChanged(accentHint, text);
}

void pickAccent(lv_event_t *e) {
    const uint32_t old = theme::accent();
    settings::get().accent = (int)(intptr_t)lv_event_get_user_data(e);
    if (theme::accent() == old) return;
    settings::changed();
    theme::applyAccent();

    // Colours set on objects don't follow the theme. App screens and pages
    // are built anew when opened, home has none and applySettings() sets the
    // ambient screen's, so only this page needs them swapped. Only this page:
    // the Settings tiles under it have colours an accent can equal (Yellow...),
    // as do the swatches here, which markAccent() puts back.
    lv_obj_t *page = lv_event_get_target_obj(e);
    lv_obj_t *screen = lv_obj_get_screen(page);
    while (lv_obj_get_parent(page) != screen) page = lv_obj_get_parent(page);
    theme::recolour(page, old, theme::accent());
    markAccent();
    ui::applySettings();  // also has home's snapshot for slides taken again
}

// Home tiles: up and down buttons rather than dragging, which is fiddly on a
// small screen. The rows stay, their tiles change.
lv_obj_t *homeTilesValue;  // on the Display page
lv_obj_t *tileRows[settings::kHomeTiles];

void showHomeTilesValue() {
    App order[settings::kHomeTiles];
    homeTileOrder(order);
    char text[64];
    snprintf(text, sizeof(text), "%s, %s, %s first", appTile(order[0]).name, appTile(order[1]).name,
             appTile(order[2]).name);
    setTextIfChanged(homeTilesValue, text);
}

void fillTileRows() {
    App order[settings::kHomeTiles];
    homeTileOrder(order);
    for (int i = 0; i < settings::kHomeTiles; i++) {
        setTextIfChanged(lv_obj_get_child(tileRows[i], 0), appTile(order[i]).symbol);
        setTextIfChanged(lv_obj_get_child(tileRows[i], 1), appTile(order[i]).name);
    }
}

// User data: the row's index, times 2, plus 1 for down
void moveTile(lv_event_t *e) {
    const int code = (int)(intptr_t)lv_event_get_user_data(e);
    const int i = code / 2, j = i + (code % 2 ? 1 : -1);
    if (j < 0 || j >= settings::kHomeTiles) return;
    App order[settings::kHomeTiles];
    homeTileOrder(order);
    const App moved = order[i];
    order[i] = order[j];
    order[j] = moved;
    for (int k = 0; k < settings::kHomeTiles; k++) settings::get().homeTiles[k] = static_cast<uint8_t>(order[k]);
    settings::changed();
    fillTileRows();
    ui::applySettings();
}

void openHomeTiles(lv_obj_t *screen) {
    lv_obj_t *body = openSubpage(screen, "Home tiles", showHomeTilesValue, kCaption);
    for (int i = 0; i < settings::kHomeTiles; i++) {
        static const char *const kPageNames[] = {"FIRST PAGE", "SECOND PAGE", "THIRD PAGE"};
        if (i % 3 == 0) addSection(body, kPageNames[i / 3]);
        lv_obj_t *card = addCard(body, nullptr, false);
        lv_obj_t *icon = addText(card, "", &font_ui_22, theme::kText);
        lv_obj_set_width(icon, 34);
        lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t *name = addText(card, "", &font_ui_22, theme::kText);
        lv_obj_set_flex_grow(name, 1);
        for (int down = 0; down < 2; down++) {
            const bool off = down ? i == settings::kHomeTiles - 1 : i == 0;
            lv_obj_t *btn = addRoundButton(card, 46, down ? LV_SYMBOL_DOWN : LV_SYMBOL_UP, false,
                                           off ? nullptr : moveTile, (void *)(intptr_t)(i * 2 + down));
            if (off) {
                lv_obj_set_style_opa(btn, LV_OPA_30, 0);
                lv_obj_set_clickable(btn, false);
            }
        }
        tileRows[i] = card;
    }
    fillTileRows();
    addHint(body, "The first page shows when Home opens; swipe the tiles for the rest.");
}

uint32_t shownPictures;

// The designs, then the pictures, then "Your picture"
void fillWallpapers(lv_obj_t *grid) {
    lv_obj_clean(grid);
    const int picks = wallpaper::count() + 1;  // and "Your picture"
    wallpaper::freeGoneThumbnails();           // the images showing them just went
    setGrid(grid, 2, (picks + 1) / 2);  // two across, big enough to tell apart
    wallpaperPicks.clear();
    for (int i = 0; i < picks; i++) {
        const bool add = i == picks - 1;
        lv_obj_t *pick = addColumn(grid, 6);
        lv_obj_set_flex_align(pick, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_grid_cell(pick, LV_GRID_ALIGN_STRETCH, i % 2, 1, LV_GRID_ALIGN_START, i / 2, 1);
        lv_obj_set_clickable(pick, true);

        // Square, like the screen it previews
        lv_obj_t *frame = lv_obj_create(pick);
        lv_obj_remove_style_all(frame);
        lv_obj_set_size(frame, wallpaper::kThumbSize + 6, wallpaper::kThumbSize + 6);
        lv_obj_set_style_border_width(frame, 3, 0);
        lv_obj_set_style_border_opa(frame, LV_OPA_COVER, 0);
        lv_obj_set_style_border_post(frame, true, 0);  // over the picture, corners and all
        lv_obj_set_style_radius(frame, 18, 0);
        lv_obj_set_style_clip_corner(frame, true, 0);
        lv_obj_set_clickable(frame, false);
        lv_obj_set_scrollable(frame, false);
        if (add) {
            // Pictures come from a computer: this opens the upload page
            lv_obj_set_style_border_color(frame, lv_color_hex(theme::kSurfaceHi), 0);
            lv_obj_set_style_bg_color(frame, lv_color_hex(theme::kSurface), 0);
            lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
            lv_obj_center(addText(frame, LV_SYMBOL_PLUS, &font_ui_30, theme::kTextDim));
            addText(pick, "Your picture", &font_ui_22, theme::kTextDim);
            lv_obj_add_event_cb(
                pick, [](lv_event_t *e) { openUploadPage(lv_obj_get_screen(lv_event_get_target_obj(e))); },
                LV_EVENT_CLICKED, nullptr);
            break;
        }
        lv_obj_t *img = lv_image_create(frame);
        lv_image_set_src(img, wallpaper::thumbnail(i));
        lv_obj_center(img);
        lv_obj_t *check = addIconCircle(frame, LV_SYMBOL_OK, 34, theme::accent(), LV_OPA_COVER, theme::kBg);
        lv_obj_set_style_text_font(lv_obj_get_child(check, 0), &font_ui_20, 0);
        lv_obj_align(check, LV_ALIGN_TOP_RIGHT, -5, 5);

        lv_obj_t *label = addText(pick, wallpaper::name(i), &font_ui_22, theme::kTextDim);
        lv_obj_set_width(label, wallpaper::kThumbSize);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_add_event_cb(
            pick,
            [](lv_event_t *e) {
                wallpaper::choose((int)(intptr_t)lv_event_get_user_data(e));
                markWallpaper();
                ui::applySettings();
            },
            LV_EVENT_CLICKED, (void *)(intptr_t)i);
        wallpaperPicks.push_back(pick);
    }
    markWallpaper();
}

void openDisplay(lv_obj_t *screen) {
    lv_obj_t *body = page(screen, "Display");
    addSection(body, "BRIGHTNESS");
    addBigSlider(body, 5, 100, toPercent(settings::get().brightness), SYM_SUN, [](lv_event_t *e) {
        settings::get().brightness = toLevel(lv_slider_get_value(lv_event_get_target_obj(e)));
        display::setBrightness(ui::brightnessFor(settings::get().brightness));
        settings::changed();
    });
    // The light sensor's: both brightness levels are then for a normally lit room
    if (light::present()) {
        switchCard(body, "Automatic brightness", settings::get().autoBrightness, [](lv_event_t *e) {
            settings::get().autoBrightness = checked(e);
            settings::changed();
        });
        addHint(body, "Follows the room: dimmer in the dark, brighter in daylight. The bar sets it for a "
                      "normally lit room.");
    } else {
        lv_obj_t *card = addCard(body, "Automatic brightness", false);
        addText(card, "No sensor", &font_ui_22, theme::kTextDim);
        addHint(body, "Needs a BH1750 light sensor on the radio's I2C bus.");
    }

    addSection(body, "WALLPAPER");
    lv_obj_t *grid = lv_obj_create(body);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_set_style_pad_row(grid, 14, 0);
    lv_obj_set_scrollable(grid, false);
    // Its children go right after this, and nothing is drawn in between
    lv_obj_add_event_cb(grid, [](lv_event_t *) { wallpaper::freeThumbnails(); }, LV_EVENT_DELETE, nullptr);
    fillWallpapers(grid);
    // A picture sent or deleted while this is open: the grid follows
    shownPictures = wallpaper::revision();
    lv_timer_t *follow = every(grid, 500, [](lv_timer_t *t) {
        if (wallpaper::revision() == shownPictures) return;
        shownPictures = wallpaper::revision();
        fillWallpapers(static_cast<lv_obj_t *>(lv_timer_get_user_data(t)));
    });
    lv_timer_set_user_data(follow, grid);

    addSection(body, "ACCENT COLOUR");
    // Two rows of four, big enough to hit easily
    lv_obj_t *swatches = lv_obj_create(body);
    lv_obj_remove_style_all(swatches);
    lv_obj_set_size(swatches, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_scrollable(swatches, false);
    setGrid(swatches, 4, (theme::kAccentCount + 3) / 4);
    lv_obj_set_style_pad_row(swatches, 16, 0);
    // Room for the ring around the selected one (3 px out, 3 px wide) inside
    // the grid: drawn past it, it got clipped even with overflow visible
    lv_obj_set_style_pad_ver(swatches, 6, 0);
    for (int i = 0; i < theme::kAccentCount; i++) {
        const uint32_t colour = theme::kAccents[i].colour;
        lv_obj_t *pick = addIconCircle(swatches, LV_SYMBOL_OK, 66, colour, LV_OPA_COVER, theme::kBg);
        lv_obj_set_grid_cell(pick, LV_GRID_ALIGN_CENTER, i % 4, 1, LV_GRID_ALIGN_CENTER, i / 4, 1);
        lv_obj_set_style_text_font(lv_obj_get_child(pick, 0), &font_ui_30, 0);
        lv_obj_set_style_outline_color(pick, lv_color_hex(theme::kText), 0);
        lv_obj_set_style_outline_opa(pick, LV_OPA_COVER, 0);
        lv_obj_set_style_outline_pad(pick, 3, 0);
        lv_obj_set_clickable(pick, true);
        lv_obj_set_ext_click_area(pick, 8);  // up to the ring, about half the gap
        lv_obj_add_event_cb(pick, pickAccent, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        accentPicks[i] = pick;
    }
    accentHint = addHint(body, "");
    markAccent();

    addSection(body, "HOME SCREEN");
    lv_obj_t *tiles = makeTappable(addCard(body, "Home tiles", false), [](lv_event_t *e) {
        openHomeTiles(lv_obj_get_screen(lv_event_get_target_obj(e)));
    });
    // Takes what the title leaves, wrapping onto a second line when it must
    homeTilesValue = addValue(tiles, "");
    lv_obj_set_width(homeTilesValue, 0);
    lv_obj_set_flex_grow(homeTilesValue, 1);
    lv_obj_set_style_max_width(homeTilesValue, LV_COORD_MAX, 0);
    lv_label_set_long_mode(homeTilesValue, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(homeTilesValue, LV_TEXT_ALIGN_RIGHT, 0);
    showHomeTilesValue();
}

// ----- Clock screen -----

lv_obj_t *modeTiles[3];
lv_obj_t *modeClocks[2];
lv_obj_t *faceCard;
lv_obj_t *faceValue;

void markMode() {
    const int selected = static_cast<int>(settings::get().ambientMode);
    for (int i = 0; i < 3; i++) {
        const bool on = i == selected;
        lv_obj_set_style_border_color(modeTiles[i], lv_color_hex(on ? theme::accent() : theme::kSurface), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(modeTiles[i], 1),
                                    lv_color_hex(on ? theme::accent() : theme::kText), 0);
    }
    lv_obj_set_hidden(faceCard, settings::get().ambientMode == settings::AmbientMode::ScreenOff);
}

void showFaceValue() { setTextIfChanged(faceValue, faceName()); }

// The faces, built-in then the card's, drawn once each when the page opens
// (small images, freed with it) and again when the card's change
constexpr int32_t kFaceThumb = 202;  // two across, each filling its half, inside a 2 px border
struct FacePick {
    lv_obj_t *pick;
    lv_obj_t *image;
    lv_draw_buf_t *preview;
    settings::ClockFace face;
    uint16_t card;  // for Card
};
std::vector<FacePick> facePicks;
lv_obj_t *nightColourBox;  // under the grid, for Night and Segment
uint32_t shownCardFaces;

bool isSelected(const FacePick &p) {
    const auto &s = settings::get();
    return p.face == faceInUse() && (p.face != settings::ClockFace::Card || p.card == s.clockFaceFile);
}

void markFace() {
    for (const FacePick &p : facePicks) {
        const uint32_t colour = isSelected(p) ? theme::accent() : theme::kSurfaceHi;
        lv_obj_set_style_border_color(lv_obj_get_child(p.pick, 0), lv_color_hex(colour), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(p.pick, 1),
                                    lv_color_hex(isSelected(p) ? theme::accent() : theme::kTextDim), 0);
    }
    lv_obj_set_hidden(nightColourBox, !isNightFace(faceInUse()));
}

void showFacePreview(FacePick &p) {
    // The page draws one per face as it opens, ~0.1-0.3 s each: with many
    // faces on the card that adds up past the watchdog's 5 s
    watchdog::feed();
    lv_draw_buf_t *old = p.preview;
    const bool withWeather = settings::get().ambientMode == settings::AmbientMode::ClockWeather;
    p.preview = clockFacePreview(p.face, p.card, withWeather, kFaceThumb);
    lv_image_set_src(p.image, p.preview);  // before the old one goes
    if (old) lv_draw_buf_destroy(old);
}

void freeFacePreviews() {
    for (FacePick &p : facePicks) {
        if (p.preview) lv_draw_buf_destroy(p.preview);
    }
    facePicks.clear();
}

void fillFaces(lv_obj_t *grid) {
    lv_obj_clean(grid);  // the images go before the previews they show
    freeFacePreviews();
    const std::vector<card_faces::Face> cardFaces = card_faces::list();
    const int count = card_faces::kBuiltInCount + cardFaces.size();
    setGrid(grid, 2, (count + 1) / 2);
    facePicks.reserve(count);
    for (int i = 0; i < count; i++) {
        const bool builtIn = i < card_faces::kBuiltInCount;
        FacePick p = {};
        p.face = builtIn ? static_cast<settings::ClockFace>(i) : settings::ClockFace::Card;
        p.card = builtIn ? 0 : cardFaces[i - card_faces::kBuiltInCount].id;
        p.pick = addColumn(grid, 6);
        lv_obj_set_flex_align(p.pick, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_grid_cell(p.pick, LV_GRID_ALIGN_STRETCH, i % 2, 1, LV_GRID_ALIGN_START, i / 2, 1);
        lv_obj_set_clickable(p.pick, true);

        lv_obj_t *frame = lv_obj_create(p.pick);
        lv_obj_remove_style_all(frame);
        lv_obj_set_size(frame, kFaceThumb + 4, kFaceThumb + 4);
        lv_obj_set_style_border_width(frame, 2, 0);
        lv_obj_set_style_border_opa(frame, LV_OPA_COVER, 0);
        // Over the preview: drawn first, the square image covered the rounded corners
        lv_obj_set_style_border_post(frame, true, 0);
        lv_obj_set_style_radius(frame, 20, 0);
        lv_obj_set_style_clip_corner(frame, true, 0);
        lv_obj_set_clickable(frame, false);
        lv_obj_set_scrollable(frame, false);
        p.image = lv_image_create(frame);
        lv_obj_center(p.image);
        showFacePreview(p);

        const char *label = builtIn ? card_faces::kBuiltIn[i] : cardFaces[i - card_faces::kBuiltInCount].name.c_str();
        lv_obj_t *name = addText(p.pick, label, &font_ui_20, theme::kTextDim);
        lv_obj_set_width(name, kFaceThumb);
        lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_add_event_cb(
            p.pick,
            [](lv_event_t *e) {
                const FacePick &p = facePicks[(intptr_t)lv_event_get_user_data(e)];
                auto &s = settings::get();
                s.clockFace = p.face;
                if (p.face == settings::ClockFace::Card) s.clockFaceFile = p.card;
                settings::changed();
                markFace();
                ui::applySettings();
            },
            LV_EVENT_CLICKED, (void *)(intptr_t)i);
        facePicks.push_back(p);
    }
    markFace();
}

void openClockFace(lv_obj_t *screen) {
    lv_obj_t *body = openSubpage(screen, "Clock face", showFaceValue, kCaption);
    storage::checkForCard();  // one put in since shows its faces here
    lv_obj_t *grid = lv_obj_create(body);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(grid, 12, 0);  // a little room under the title
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_set_style_pad_row(grid, 14, 0);
    lv_obj_set_scrollable(grid, false);
    lv_obj_add_event_cb(grid, [](lv_event_t *) { freeFacePreviews(); }, LV_EVENT_DELETE, nullptr);

    nightColourBox = addColumn(body, 10);
    addSection(nightColourBox, "COLOUR");
    addChips(nightColourBox, kNightColourNames, 2, 2, static_cast<int>(settings::nightColour()), false, 54,
             [](int i) {
                 settings::get().nightColour = static_cast<settings::NightColour>(i);
                 settings::changed();
                 for (FacePick &p : facePicks) {
                     if (isNightFace(p.face)) showFacePreview(p);
                 }
                 ui::applySettings();
             });
    fillFaces(grid);
    // A face sent or deleted from the upload page while this is open: the grid follows
    shownCardFaces = card_faces::revision();
    lv_timer_t *follow = every(grid, 500, [](lv_timer_t *t) {
        if (card_faces::revision() == shownCardFaces) return;
        shownCardFaces = card_faces::revision();
        fillFaces(static_cast<lv_obj_t *>(lv_timer_get_user_data(t)));
    });
    lv_timer_set_user_data(follow, grid);
    addHint(body, "Every face moves a few pixels each minute, so the screen doesn't burn in. Make your own on "
                  "the upload page: start it in the Display settings, under Your picture.");
}

void showModeClocks(lv_timer_t *) {
    char text[16];
    clockText(text, sizeof(text), settings::get().ambientSeconds);
    for (lv_obj_t *c : modeClocks) setTextIfChanged(c, text);
}

void openClockScreen(lv_obj_t *screen) {
    const auto &s = settings::get();
    lv_obj_t *body = page(screen, "Clock screen");
    addHint(body, "Shows when the radio has been left alone for a while. Tap or press a button to wake it.");

    addSection(body, "SHOW");
    lv_obj_t *modes = lv_obj_create(body);
    lv_obj_remove_style_all(modes);
    lv_obj_set_size(modes, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(modes, 10, 0);
    lv_obj_set_scrollable(modes, false);
    setGrid(modes, 3, 1);

    weather::Forecast f;
    const bool haveWeather = weather::get(f);
    char temp[12] = "–°";
    if (haveWeather) formatTemp(temp, sizeof(temp), f.now.temp);

    for (int i = 0; i < 3; i++) {
        lv_obj_t *tile = addColumn(modes, 10);
        lv_obj_set_grid_cell(tile, LV_GRID_ALIGN_STRETCH, i, 1, LV_GRID_ALIGN_START, 0, 1);
        lv_obj_set_style_bg_color(tile, lv_color_hex(theme::kSurface), 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(tile, 20, 0);
        lv_obj_set_style_border_width(tile, 2, 0);
        lv_obj_set_style_pad_all(tile, 10, 0);
        lv_obj_set_style_pad_bottom(tile, 12, 0);
        lv_obj_set_flex_align(tile, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_clickable(tile, true);

        // A tiny clock screen: black, like the real one
        lv_obj_t *preview = addColumn(tile, 2);
        lv_obj_set_height(preview, 82);
        lv_obj_set_flex_align(preview, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(preview, lv_color_hex(theme::kBg), 0);
        lv_obj_set_style_bg_opa(preview, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(preview, 1, 0);
        lv_obj_set_style_border_color(preview, lv_color_hex(theme::kSurfaceHi), 0);
        lv_obj_set_style_radius(preview, 12, 0);
        if (i < 2) modeClocks[i] = addText(preview, "", &font_ui_22, theme::kText);
        if (i == 1) {
            lv_obj_t *w = addRow(preview, 4);
            lv_obj_set_width(w, LV_SIZE_CONTENT);
            addText(w, weather::icon(haveWeather ? f.now.code : 2, haveWeather ? f.now.isDay : true),
                    &font_weather_30, theme::kTextDim);
            addText(w, temp, &font_ui_20, theme::kTextDim);
        }
        addText(tile, kModeLabels[i], &font_ui_22, theme::kText);
        lv_obj_add_event_cb(
            tile,
            [](lv_event_t *e) {
                settings::get().ambientMode = static_cast<settings::AmbientMode>((intptr_t)lv_event_get_user_data(e));
                settings::changed();
                markMode();
                ui::applySettings();
            },
            LV_EVENT_CLICKED, (void *)(intptr_t)i);
        modeTiles[i] = tile;
    }
    faceCard = makeTappable(addCard(body, "Face", false), [](lv_event_t *e) {
        openClockFace(lv_obj_get_screen(lv_event_get_target_obj(e)));
    });
    faceValue = addValue(faceCard, "");
    showFaceValue();
    markMode();
    showModeClocks(nullptr);
    every(modes, 1000, showModeClocks);

    addSection(body, "START AFTER");
    int selected = kTimeoutCount - 1;
    for (int i = 0; i < kTimeoutCount; i++) {
        if (kTimeouts[i] == s.ambientTimeoutS) selected = i;
    }
    addChips(body, kTimeoutLabels, kTimeoutCount, 3, selected, false, 54, [](int i) {
        settings::get().ambientTimeoutS = kTimeouts[i];
        settings::changed();
    });

    // The panel dims to this level while dragging, so it's seen for real
    addSection(body, "BRIGHTNESS");
    lv_obj_t *dim = addBigSlider(body, 2, 100, toPercent(s.ambientBrightness), SYM_MOON, [](lv_event_t *e) {
        settings::get().ambientBrightness = toLevel(lv_slider_get_value(lv_event_get_target_obj(e)));
        display::setBrightness(ui::brightnessFor(settings::get().ambientBrightness, true));
        settings::changed();
    });
    for (lv_event_code_t code : {LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST}) {
        lv_obj_add_event_cb(dim, [](lv_event_t *) { display::setBrightness(ui::brightnessFor(settings::get().brightness)); }, code,
                            nullptr);
    }
    addHint(body, settings::get().autoBrightness && light::present()
                      ? "For a normally lit room: in the dark it goes dimmer, never off, so the time stays "
                        "readable. The screen dims to this while you drag."
                      : "The screen dims to this while you drag, so you see it for real.");

    switchCard(body, "Show seconds", s.ambientSeconds, [](lv_event_t *e) {
        settings::get().ambientSeconds = checked(e);
        settings::changed();
        showModeClocks(nullptr);
        ui::applySettings();
    });

    switchCard(body, "Lock", s.screenLock, [](lv_event_t *e) {
        settings::get().screenLock = checked(e);
        settings::changed();
    });
    addHint(body, "A tap then only shows \"slide to unlock\": slide it to get to Home. The buttons and a "
                  "ringing alarm work without it.");
}

// ----- Date & time -----

lv_obj_t *dtTime, *dtDate, *dtSyncIcon, *dtSync;

void showDateTime(lv_timer_t *) {
    struct tm t;
    char buf[64];
    if (localNow(t)) {
        formatTime(buf, sizeof(buf), t);
        setTextIfChanged(dtTime, buf);
        formatDate(buf, sizeof(buf), t);
        snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), " %d", t.tm_year + 1900);
        setTextIfChanged(dtDate, buf);
    } else {
        setTextIfChanged(dtTime, "--:--");
        setTextIfChanged(dtDate, "The clock isn't set yet");
    }

    const time_t synced = wifi::lastTimeSync();
    if (synced) {
        char at[16];
        formatClock(at, sizeof(at), synced);
        snprintf(buf, sizeof(buf), "From the internet · checked at %s", at);
    } else {
        snprintf(buf, sizeof(buf), rtc::timeValid() ? "From the radio's own clock" : "Waiting for Wi-Fi");
    }
    setTextIfChanged(dtSync, buf);
    lv_obj_set_hidden(dtSyncIcon, !synced);
}

// The firmware has one zone built in so far (see rtc.cpp)
const char *zoneName() {
    const char *tz = rtc::tzString();
    if (!strncmp(tz, "CET", 3)) return "Central European";
    if (!strncmp(tz, "GMT", 3)) return "UK";
    if (!strncmp(tz, "EET", 3)) return "Eastern European";
    return tz;
}

void openDateTime(lv_obj_t *screen) {
    lv_obj_t *body = page(screen, "Date & time");
    lv_obj_t *card = addCard(body, nullptr, true);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_gap(card, 2, 0);
    dtTime = addText(card, "", &font_ui_50, theme::kText);
    dtDate = addText(card, "", &font_ui_22, theme::kText);
    lv_obj_t *sync = addRow(card, 8);
    lv_obj_set_style_pad_top(sync, 8, 0);
    dtSyncIcon = addText(sync, LV_SYMBOL_OK, &font_ui_20, theme::accent());
    dtSync = addText(sync, "", &font_ui_20, theme::kTextDim);
    showDateTime(nullptr);
    every(card, 1000, showDateTime);

    addValue(addCard(body, "Time zone", false), zoneName());
    lv_obj_t *byHand = makeTappable(addCard(body, "Set by hand", false), [](lv_event_t *e) {
        openTimeDialog(lv_obj_get_screen(lv_event_get_target_obj(e)), [] {
            showDateTime(nullptr);
            ui::applySettings();
        });
    });
    addText(byHand, LV_SYMBOL_RIGHT, &font_ui_22, theme::kTextDim);
    addHint(body, "Only needed without Wi-Fi. Otherwise the radio sets its clock from the internet.");
}

// ----- About -----

lv_obj_t *backupValue, *restoreValue;
uint32_t restoreArmedAt = 0;  // restore asks for a second tap within kConfirmMs
constexpr uint32_t kConfirmMs = 4000;

void showBackup() {
    char buf[48] = "No card";
    if (storage::hasCard()) {
        const time_t made = backup::lastSaved();
        if (made > 100000) {
            struct tm t;
            localtime_r(&made, &t);
            char date[32], clock[16];
            formatDate(date, sizeof(date), t);
            formatTime(clock, sizeof(clock), t);
            snprintf(buf, sizeof(buf), "%s, %s", strchr(date, ' ') + 1, clock);  // without the weekday
        } else {
            snprintf(buf, sizeof(buf), made ? "Date unknown" : "None yet");
        }
    }
    setTextIfChanged(backupValue, buf);
    const bool armed = restoreArmedAt && millis() - restoreArmedAt < kConfirmMs;
    setTextIfChanged(restoreValue, armed                            ? "Tap again to restore"
                                   : backup::lastSaved() ? "Restarts the radio"
                                                         : "Nothing to restore");
}

lv_obj_t *aboutIp, *aboutUptime, *aboutMemory, *aboutBattery, *aboutUsb, *aboutChip, *aboutCard;
uint32_t shownCard;

void showAbout(lv_timer_t *) {
    char buf[64];
    setTextIfChanged(aboutIp, wifi::status() == wifi::Status::Connected ? wifi::ip().c_str() : "–");

    const uint32_t minutes = millis() / 60000;
    if (minutes < 60) {
        snprintf(buf, sizeof(buf), "%lu min", minutes);
    } else if (minutes < 24 * 60) {
        snprintf(buf, sizeof(buf), "%lu h %lu min", minutes / 60, minutes % 60);
    } else {
        snprintf(buf, sizeof(buf), "%lu days %lu h", minutes / (24 * 60), minutes / 60 % 24);
    }
    setTextIfChanged(aboutUptime, buf);

    snprintf(buf, sizeof(buf), "%lu KB RAM · %.1f MB PSRAM", ESP.getFreeHeap() / 1024,
             ESP.getFreePsram() / (1024.0f * 1024.0f));
    setTextIfChanged(aboutMemory, buf);

    const power::Battery &b = power::battery();
    if (b.present) {
        snprintf(buf, sizeof(buf), "%d%% · %.2f V%s", b.percent, b.millivolts / 1000.0f,
                 b.charging ? " · charging" : b.usb ? " · on USB" : "");
    } else {
        snprintf(buf, sizeof(buf), "None");
    }
    setTextIfChanged(aboutBattery, buf);

    // The power chip's own readings: what comes in over USB-C, and how warm it is
    const power::Readings &r = power::readings();
    if (r.usbMv) {
        snprintf(buf, sizeof(buf), "%.2f V", r.usbMv / 1000.0f);
    } else {
        snprintf(buf, sizeof(buf), "Not plugged in");
    }
    setTextIfChanged(aboutUsb, buf);
    if (isnan(r.chipC)) {
        snprintf(buf, sizeof(buf), "–");
    } else {
        snprintf(buf, sizeof(buf), "%.0f °C", r.chipC);
    }
    setTextIfChanged(aboutChip, buf);

    // Free space only when a card comes or goes: asking the card costs time
    if (storage::cardRevision() != shownCard) {
        shownCard = storage::cardRevision();
        if (storage::hasCard()) {
            const float gb = 1024.0f * 1024.0f * 1024.0f;
            snprintf(buf, sizeof(buf), "%.1f GB · %.1f GB free", SD_MMC.cardSize() / gb,
                     (SD_MMC.totalBytes() - SD_MMC.usedBytes()) / gb);
        } else {
            snprintf(buf, sizeof(buf), "None");
        }
        setTextIfChanged(aboutCard, buf);
    }
}

void openAbout(lv_obj_t *screen) {
    lv_obj_t *body = page(screen, "About");
    lv_obj_t *card = addTitleCard(body, "DIY Radio", &font_ui_30, "Waveshare ESP32-S3 AMOLED 2.16\"");

    // The version, and when it was built (__DATE__ is "Nov  1 2026")
    char built[32];
    snprintf(built, sizeof(built), "%s · %d %.3s %s", FIRMWARE_VERSION, atoi(__DATE__ + 4), __DATE__,
             __DATE__ + 7);
    lv_label_set_text(infoCard(body, "Firmware"), built);
    aboutIp = infoCard(body, "IP address");
    aboutUptime = infoCard(body, "Running for");
    aboutMemory = infoCard(body, "Memory free");
    aboutBattery = infoCard(body, "Battery");
    aboutUsb = infoCard(body, "USB power");
    aboutChip = infoCard(body, "Power chip");
    aboutCard = infoCard(body, "microSD card");
    storage::checkForCard();  // one put in since shows up here
    shownCard = storage::cardRevision() - 1;
    showAbout(nullptr);
    every(card, 1000, showAbout);

    // On the card, for after erasing the board or for a second radio
    addSection(body, "BACKUP");
    backupValue = addValue(makeTappable(addCard(body, "Back up now", false),
                                        [](lv_event_t *) {
                                            setTextIfChanged(backupValue, backup::save() ? "Done" : "Failed");
                                        }),
                           "");
    restoreValue = addValue(makeTappable(addCard(body, "Restore", false),
                                         [](lv_event_t *) {
                                             if (restoreArmedAt && millis() - restoreArmedAt < kConfirmMs) {
                                                 backup::restore();  // restarts when it works
                                                 setTextIfChanged(restoreValue, "Failed");
                                                 restoreArmedAt = 0;
                                                 return;
                                             }
                                             restoreArmedAt = backup::lastSaved() ? millis() : 0;
                                             showBackup();
                                         }),
                            "");
    restoreArmedAt = 0;
    showBackup();
    every(backupValue, 1000, [](lv_timer_t *) {
        // "Done" or "Failed" stays a moment, then the usual line comes back
        static uint32_t ticks = 0;
        if (++ticks % 3 == 0) showBackup();
    });
    addHint(body, "On the microSD card: settings, alarms, favourites and the weather location. Not the Wi-Fi password.");

    addSection(body, "DEVELOPER");
    // As set in the power chip, which keeps it while a battery is connected
    const power::ChargeSettings c = power::chargeSettings();
    char text[48];
    snprintf(text, sizeof(text), "to %u.%02u V · %u mA · stops at %u mA%s", c.targetMv / 1000,
             c.targetMv % 1000 / 10, c.currentMa, c.stopMa, c.stopOn ? "" : " (off)");
    lv_label_set_text(infoCard(body, "Charging"), text);
    snprintf(text, sizeof(text), "off below %u.%02u V · USB %u mA", c.cutoffMv / 1000,
             c.cutoffMv % 1000 / 10, c.usbLimitMa);
    lv_label_set_text(infoCard(body, "Power"), text);
    switchCard(body, "Performance overlay", settings::get().perfOverlay, [](lv_event_t *e) {
        settings::get().perfOverlay = checked(e);
        settings::changed();
        ui::applySettings();
    });
}

// ----- Main -----

struct TileInfo {
    const char *name;
    const char *icon;
    uint32_t colour;
    void (*open)(lv_obj_t *screen);
};

const TileInfo kTiles[kTileCount] = {
    {"Wi-Fi", LV_SYMBOL_WIFI, theme::kCyan, [](lv_obj_t *s) { openWifiPage(s, refreshTiles); }},
    {"Display", SYM_SUN, theme::kYellow, openDisplay},
    {"Clock screen", SYM_MOON, theme::kPurple, openClockScreen},
    {"Date & time", SYM_CLOCK, theme::kOrange, openDateTime},
    {"Weather location", LV_SYMBOL_GPS, theme::kGreen, [](lv_obj_t *s) { openLocationPage(s, refreshTiles); }},
    {"About", SYM_INFO, theme::kTextDim, openAbout},
};

}  // namespace

namespace {
bool uploadPageNext = false;
}  // namespace

void openUploadPageNext() { uploadPageNext = true; }

lv_obj_t *createSettings() {
    lv_obj_t *body = createAppScreen("Settings");
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 10, 0);

    for (int i = 0; i < kTileCount; i++) {
        // One per row, big enough to read: icon, name and value, then a chevron
        lv_obj_t *tile = addRow(body, 18);
        lv_obj_set_height(tile, 102);
        lv_obj_set_style_bg_color(tile, lv_color_hex(theme::kSurface), 0);
        lv_obj_set_style_bg_color(tile, lv_color_hex(theme::kSurfaceHi), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(tile, 20, 0);
        lv_obj_set_style_pad_hor(tile, 18, 0);
        lv_obj_set_clickable(tile, true);

        lv_obj_t *icon = addIconCircle(tile, kTiles[i].icon, 62, kTiles[i].colour, LV_OPA_20, kTiles[i].colour);
        lv_obj_set_style_text_font(lv_obj_get_child(icon, 0), &font_ui_30, 0);
        // Takes what the icon and chevron leave; the value gets dots when too long
        lv_obj_t *text = addColumn(tile, 2);
        lv_obj_set_width(text, 0);
        lv_obj_set_flex_grow(text, 1);
        lv_obj_t *name = addText(text, kTiles[i].name, &font_ui_30, theme::kText);
        lv_obj_set_width(name, LV_PCT(100));
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        tileValues[i] = addText(text, "", &font_ui_22, theme::kTextDim);
        lv_obj_set_width(tileValues[i], LV_PCT(100));
        lv_label_set_long_mode(tileValues[i], LV_LABEL_LONG_DOT);
        addText(tile, LV_SYMBOL_RIGHT, &font_ui_22, theme::kTextDim);

        lv_obj_add_event_cb(
            tile,
            [](lv_event_t *e) {
                const int i = (int)(intptr_t)lv_event_get_user_data(e);
                kTiles[i].open(lv_obj_get_screen(lv_event_get_target_obj(e)));
            },
            LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    lv_obj_add_event_cb(
        body, [](lv_event_t *) { tileValues[0] = nullptr; }, LV_EVENT_DELETE, nullptr);
    refreshTiles();
    every(body, 1000, [](lv_timer_t *) { refreshTiles(); });
    if (uploadPageNext) {
        uploadPageNext = false;
        openUploadPage(lv_obj_get_screen(body));
    }
    return lv_obj_get_screen(body);
}

}  // namespace ui::screens
