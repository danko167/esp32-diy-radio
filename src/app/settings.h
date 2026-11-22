#pragma once

#include <stdint.h>

namespace settings {

enum class AmbientMode : uint8_t { Clock, ClockWeather, ScreenOff };
// Card: a face file on the microSD card (clockFaceFile, see ui/card_faces.h)
enum class ClockFace : uint8_t { Digital, Analog, Split, Night, Segment, Card = 255 };
// The night faces' (Night and Segment) colour
enum class NightColour : uint8_t { Red, Green };

constexpr int kHomeTiles = 8;

// Persisted in NVS as one blob. New fields go at the end, so older saves load
// them as defaults. If a new field lands in old padding, or fields change,
// bump kVersion in settings.cpp and migrate there.
struct Settings {
    uint8_t brightness = 180;         // normal use, 0..255
    uint8_t ambientBrightness = 40;   // ambient clock screen
    uint16_t ambientTimeoutS = 60;    // inactivity before ambient, 0 = never
    AmbientMode ambientMode = AmbientMode::Clock;
    uint8_t wallpaper = 1;  // a design, or 255 for a picture (wallpaperPicture)
    bool unused24h = true;  // was use24h, till 12-hour mode went (6 November): kept so the fields after stay put
    bool ambientSeconds = false;
    bool perfOverlay = false;  // LVGL's FPS/CPU readout, for development
    uint8_t volume = 40;  // radio and media, 0..100
    // Since v3's 10 bytes, which end without padding, so v3 saves load these as defaults
    uint8_t accent = 0;  // index into the accent colours, 0 = the original sky blue
    ClockFace clockFace = ClockFace::Digital;
    uint8_t unused[6] = {};  // the tile order up to v4, before the Recorder: kept so the fields after stay put
    // Lands in the padding at the end of v3 saves, so it can load as junk:
    // harmless, as it's only read when wallpaper is 255, which v3 never saved
    uint16_t wallpaperPicture = 0;  // when wallpaper is 255: the picture's id (see ui/wallpaper.h)
    uint8_t uploadIdleMinutes = 15;  // the upload server stops after this long without activity
    // Since v4: in v3's padding, so v3 saves get the default (see load())
    NightColour nightColour = NightColour::Red;
    // Lands in the padding at the end of v4 saves, so it can load as junk:
    // harmless, as it's only read when clockFace is Card, which v4 never saved
    uint16_t clockFaceFile = 0;  // when clockFace is Card: the face's id
    // Since v5: ui::App values, in the order Home shows them (6 is the Recorder,
    // 7 the Level since v6, its byte in v5's padding: see load())
    uint8_t homeTiles[kHomeTiles] = {0, 1, 2, 3, 4, 6, 7, 5};
    // Since v6
    bool screenLock = false;  // a tap on the clock screen only shows "slide to unlock"
    // The Level's "Set as level", in hundredths of a degree: what the sensor
    // reads when the radio stands level (left to right, front to back)
    int16_t levelOffsetX = 0;
    int16_t levelOffsetY = 0;
    // After v6's end, so v6 saves load it off
    bool autoRotate = false;  // the screen follows the radio stood on its left or right side
    // Since v7, in v6's padding (see load()): both brightness levels are then
    // for a normally lit room, adjusted to the light sensor's reading
    bool autoBrightness = false;
};

// settings' nightColour, Red for anything it doesn't know
NightColour nightColour();

Settings &get();
void load();

// Schedule a save. The write happens a few seconds later, so dragging
// a slider doesn't wear out the flash.
void changed();
void loop();
void saveNow();  // a scheduled save right away (before a backup)

}  // namespace settings
