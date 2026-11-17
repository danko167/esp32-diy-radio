// Internal to the ambient screen: its clock faces, which screen_ambient.cpp
// builds, updates and shifts. The built-in ones and what all faces share are
// in ambient_faces.cpp, the faces from the microSD card in ambient_card_face.cpp.
#pragma once

#include <lvgl.h>
#include <time.h>

#include <vector>

#include "app/settings.h"
#include "net/weather.h"

namespace ui::screens::ambient {

// An lv_line from one point to another, its object only as big as the stroke,
// so moving a hand redraws just where it was and where it goes
struct Stroke {
    lv_obj_t *line;
    lv_point_precise_t points[2];
};

// A card face's element that changes: what x, y and align place, and its labels
struct Piece {
    enum Kind : uint8_t { Time, Date, Alarm, Weather, Analog } kind;
    uint8_t format;  // Date's: an index into kDateFormats
    bool seconds;    // Time's
    bool words;      // Weather's
    lv_obj_t *box;
    lv_obj_t *icon;
    lv_obj_t *text;
    lv_obj_t *suffix;  // Time's seconds
    int dial;          // Analog's, into Face::dials
};

// A card face's analog clock, in the dial object's own pixels
struct Dial {
    int32_t centre;
    int32_t hourLength, minuteLength, secondLength;
    Stroke hour, minute, second;  // second.line is null without one
    std::vector<Stroke> ticks;
};

// A built face; null where it has no such part
struct Face {
    lv_obj_t *root;
    settings::ClockFace kind;  // what was built: Digital when a card face wouldn't load
    // What it was built for, to know when settings or the card ask for another
    settings::ClockFace want;
    uint16_t card;
    uint32_t cardVersion;
    uint32_t accent;
    settings::NightColour colour;  // Night's and Segment's
    lv_obj_t *clock;    // Digital, Night, Segment: "17:42"; Split: hours
    lv_obj_t *minutes;  // Split
    lv_obj_t *suffix;   // seconds
    lv_obj_t *date;
    lv_obj_t *day;  // Split's day name
    lv_obj_t *alarm;
    lv_obj_t *weatherRow;
    lv_obj_t *weatherIcon;
    lv_obj_t *weatherText;
    lv_obj_t *dot;  // Analog's centre
    Stroke hourHand;
    Stroke minuteHand;
    Stroke secondHand;  // with Show seconds
    int shownSecond;     // where the second hand is, -1 for nowhere yet
    Stroke ticks[12];

    // Card faces: the root covers the screen, its pieces placed where the file
    // says. The backdrop under it, which doesn't shift, has the background.
    lv_obj_t *backdrop;
    lv_obj_t *picture;     // on the backdrop, when the face has one
    uint16_t *pixels;      // the picture's, in PSRAM while it shows
    lv_image_dsc_t image;
    std::vector<Piece> pieces;
    std::vector<Dial> dials;  // the size is set before the lines are made: they point into it
    std::vector<lv_font_t *> fonts;  // from the card, freed with the face
};

// For the screen (ambient_faces.cpp)

// Into `parent`, centred; a card face over all of it
void buildClockFace(lv_obj_t *parent, settings::ClockFace kind, uint16_t card, uint32_t cardVersion, Face &f);
// Its objects, and what a card face loaded for it
void destroyFace(Face &f);
// Once a minute (and after a rebuild): the time itself
void showTime(Face &f, const struct tm &t);
// The parts that can change within a minute, or without the time. `alarm` is
// nextAlarmText()'s, "" for none.
void showDetails(Face &f, const struct tm &t, const settings::Settings &s, const char *alarm);
// Null hides it. Digital names the weather; the others only give the temperature.
void showWeather(Face &f, const weather::Forecast *forecast);
// "Clock not set": no time, and no alarms (they can't ring without a clock)
void showNoTime(Face &f);

// A card face's picture (ambient_card_face.cpp): into its own memory while
// the face shows, 460 KB like a wallpaper
void loadPicture(Face &f);
void freePicture(Face &f);

// Between the faces' files: building blocks (ambient_faces.cpp)

lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color);
lv_obj_t *makeBox(lv_obj_t *parent, lv_flex_flow_t flow, int32_t gap, lv_flex_align_t cross);
lv_obj_t *makeLine(lv_obj_t *parent, int32_t width, uint32_t colour);
void placeStroke(Stroke &s, int32_t x1, int32_t y1, int32_t x2, int32_t y2);
// From the centre (at `centre`, `centre`) out, `angle` degrees clockwise from 12
void pointAt(int32_t centre, int32_t angle, int32_t length, int32_t &x, int32_t &y);

inline void setHidden(lv_obj_t *obj, bool hidden) {
    if (obj && lv_obj_is_hidden(obj) != hidden) lv_obj_set_hidden(obj, hidden);
}

// ...and the card faces' side of showTime() and the rest (ambient_card_face.cpp)

// False when the face file is missing or isn't one
bool buildCard(Face &f, lv_obj_t *parent, uint16_t card);
void showCardTime(Face &f, const struct tm &t);
void showCardDetails(Face &f, const struct tm &t, const settings::Settings &s, const char *alarm);
void showCardWeather(Face &f, const weather::Forecast *forecast);
void showCardNoTime(Face &f);

}  // namespace ui::screens::ambient
