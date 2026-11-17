// Internal to the UI: screen builders and shared helpers
#pragma once

#include <lvgl.h>
#include <stddef.h>
#include <time.h>

#include "app/settings.h"
#include "ui.h"

// In ui.cpp, which owns the slides between screens
namespace ui {

// For pages opening over the current screen: renders `obj` into an image,
// then slides from that image (how it looked) to how it looks now. Forward
// slides left, as when a page opens. slideChange() frees `before`.
lv_draw_buf_t *snapshot(lv_obj_t *obj);
void slideChange(lv_draw_buf_t *before, bool forward);

// The control centre (control_center.cpp), sliding down over what's showing
void openControlCenter();
void closeControlCenter();
bool controlCenterOpen();

// The clock screen's "slide to unlock" appeared or went: full speed while it
// shows, and with the clock screen off, the panel lit at the clock's level
void unlockShown(bool shown);

}  // namespace ui

namespace ui::screens {

// Persistent screens
lv_obj_t *createHome();
void refreshHome();
void tickHome();
uint32_t homeVersion();  // changes whenever anything on home changes

// Home's tile for an app: its icon (font_ui_*) and name
struct AppTile {
    const char *symbol;
    const char *name;
};
const AppTile &appTile(App app);
// The order Home shows its tiles in: settings' homeTiles when it has each app
// once, else the default one
void homeTileOrder(App out[settings::kHomeTiles]);

lv_obj_t *createAmbient();
void refreshAmbient();
void showAmbient();  // reset layout right before the ambient screen appears
void hideAmbient();  // after it went: frees what only it needs (a card face's picture)
void tickAmbient();
bool unlockShowing();  // the clock screen's "slide to unlock" (Settings > Clock screen > Lock)
// A clock face at the design's moment (17:42, alarm 07:30), drawn full size
// and shrunk into a `size` x `size` RGB565 image; lv_draw_buf_destroy() it.
// `card`: the face file's id, for ClockFace::Card.
lv_draw_buf_t *clockFacePreview(settings::ClockFace face, uint16_t card, bool withWeather, int32_t size);

// Transient app screens, deleted when leaving
lv_obj_t *createSettings();
lv_obj_t *createWeather();
lv_obj_t *createRadio();
lv_obj_t *createAlarms();
lv_obj_t *createRecorder();
lv_obj_t *createLevel();
lv_obj_t *createControlCenter();  // ui.cpp slides it in and deletes it
lv_obj_t *createPlaceholder(const char *title, const char *symbol, const char *text);

// Wi-Fi on or off (the control centre and Settings > Wi-Fi): off also stops
// what needs it, the internet radio and the upload server
void setWifiEnabled(bool on);

// Pages and dialogs that open on top of a screen
void openTimeDialog(lv_obj_t *screen, void (*onDone)());
void openWifiPage(lv_obj_t *screen, void (*onClose)());
void openLocationPage(lv_obj_t *screen, void (*onClose)());
void openUploadPage(lv_obj_t *screen);  // Settings > Display > Your picture, also from Home
void openUploadPageNext();  // the next Settings screen opens with it (Home's upload icon)
// Text entry with an on-screen keyboard; onSubmit gets the entered text
void openTextInput(lv_obj_t *screen, const char *title, bool password,
                   void (*onSubmit)(const char *text));

// App screen with a header (back button, title, optional small caption above
// it) and swipe-to-go-back. Returns the scrollable body below the header.
lv_obj_t *createAppScreen(const char *title, const char *caption = nullptr);
// Change the header of a screen from createAppScreen() (pass its body)
void setAppTitle(lv_obj_t *body, const char *title, const char *caption = nullptr);

// Full-screen page that slides in over `screen`, with its own header; the
// back button or a right swipe closes it. Returns its body (flex column).
lv_obj_t *openSubpage(lv_obj_t *screen, const char *title, void (*onClose)(),
                      const char *caption = nullptr);
void closeSubpage(lv_obj_t *anyChildOfPage);

// For a screen or page from the two above (pass its body):
lv_obj_t *addHeaderButton(lv_obj_t *body, const char *symbol, lv_event_cb_t onTap);  // top right
lv_obj_t *addPinnedTop(lv_obj_t *body, int32_t height);  // fixed area above the scrolling body
lv_obj_t *addFooter(lv_obj_t *body, int32_t height);     // fixed area at the bottom

// Settings-style building blocks
void addSection(lv_obj_t *parent, const char *title);
// Rounded card; `vertical` stacks the title above the content. Title may be null.
lv_obj_t *addCard(lv_obj_t *parent, const char *title, bool vertical);
// Card with a name and a small dim detail line under it (children 0 and 1)
lv_obj_t *addTitleCard(lv_obj_t *parent, const char *name, const lv_font_t *font, const char *detail);
lv_obj_t *makeTappable(lv_obj_t *card, lv_event_cb_t onTap, void *userData = nullptr);
lv_obj_t *addValue(lv_obj_t *card, const char *text);  // accent text on the right

// From the designs (widgets.cpp)
void setGrid(lv_obj_t *obj, int columns, int rows);  // equal columns, up to 5; up to 24 rows
// Thick bar: drag or tap anywhere, icon on the left and value % on the right
lv_obj_t *addBigSlider(lv_obj_t *parent, int32_t min, int32_t max, int32_t value, const char *icon,
                       lv_event_cb_t onChange);
// Grid of choices, one selected; filled (accent) or outlined
lv_obj_t *addChips(lv_obj_t *parent, const char *const *labels, int count, int columns, int selected,
                   bool outlined, int32_t height, void (*onPick)(int index));
void selectChip(lv_obj_t *chips, int index);
lv_obj_t *addSwitch(lv_obj_t *parent, bool on, lv_event_cb_t onChange);
// The alarm editor's roller: three rows in `font`, the middle one white on a
// lighter band, the others dim; `width` px. Set by hand uses it too.
lv_obj_t *addRoller(lv_obj_t *parent, const char *options, lv_roller_mode_t mode, int selected, int32_t width,
                    const lv_font_t *font);
lv_obj_t *addHint(lv_obj_t *parent, const char *text, int32_t lineSpace = 4);  // small dim wrapped text
lv_obj_t *addIconCircle(lv_obj_t *parent, const char *symbol, int32_t size, uint32_t colour, lv_opa_t bgOpa,
                        uint32_t iconColour);
lv_obj_t *addRow(lv_obj_t *parent, int32_t gap);     // full width, items centred vertically
lv_obj_t *addColumn(lv_obj_t *parent, int32_t gap);  // full width
lv_obj_t *addText(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t colour);
lv_obj_t *addPill(lv_obj_t *parent, const char *label, bool primary, int32_t height, lv_event_cb_t onTap);
// Circle with an icon: accent with a black icon if `primary`, else grey with the theme's white
lv_obj_t *addRoundButton(lv_obj_t *parent, int32_t size, const char *symbol, bool primary,
                         lv_event_cb_t onTap, void *userData = nullptr);

// Local time; false while the clock isn't set
bool localNow(struct tm &out);
// "14:05": the radio is 24-hour only
void formatTime(char *out, size_t size, const struct tm &t);
// formatTime() of a moment, in local time
void formatClock(char *out, size_t size, time_t when);
// "21°", rounded
void formatTemp(char *out, size_t size, float temp);
// "Sunday, 1 November"
void formatDate(char *out, size_t size, const struct tm &t);
const char *dayName(int wday, bool shortName);
// Bell + "07:30" (or "Mon 07:30" beyond a day) for
// the next alarm; false if none
bool nextAlarmText(char *out, size_t size);
// LV_SYMBOL_BATTERY_* for a level, 0..100
const char *batteryIcon(int percent);

// Only touch the label when the text really changed, to avoid redraws.
// Returns whether it changed.
bool setTextIfChanged(lv_obj_t *label, const char *text);

// Runs `tick` every `ms` for as long as `owner` exists, skipping the times
// when its screen isn't the one showing (the clock screen, a slide)
lv_timer_t *every(lv_obj_t *owner, uint32_t ms, lv_timer_cb_t tick);

}  // namespace ui::screens
