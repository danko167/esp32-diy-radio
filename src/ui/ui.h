#pragma once

#include <lvgl.h>

namespace ui {

enum class App { FmRadio, Internet, Bluetooth, Weather, Alarm, Settings, Recorder, Level };

// Builds the persistent screens and starts clock and inactivity handling.
// The display and settings must be initialised first.
void begin();

// Which way the open app leaves when going back
enum class Exit { ToRight, ToLeft };

void open(App app);                    // app slides in from the right
void back(Exit exit = Exit::ToRight);  // back to home
void sleep();        // go to the ambient screen now
void wake();         // leave the ambient screen

// Re-apply settings that change persistent screens (wallpaper, clock format)
void applySettings();

// Full-screen "Updating firmware" with progress, 0..100; -1 (it failed)
// removes it. The update blocks the main loop, so this draws immediately
// instead of waiting for LVGL's timer.
void showUpdateProgress(int percent);

void printState();  // for the dev console

// The panel's level for a brightness setting (settings' brightness or
// ambientBrightness): the setting itself, or with automatic brightness on,
// adjusted to the room. `clockScreen` lets it go lower in the dark.
uint8_t brightnessFor(uint8_t setting, bool clockScreen = false);

}  // namespace ui
