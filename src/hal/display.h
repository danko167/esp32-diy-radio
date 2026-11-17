#pragma once

#include <lvgl.h>

namespace display {

// Brings up the AMOLED, touch and LVGL. Wire must already be started.
bool begin();

// 0..255. AMOLED brightness is set in the panel itself, there is no backlight.
void setBrightness(uint8_t level);
uint8_t brightness();

// Which way the picture faces, for the radio stood on a side (auto-rotate):
// Left is the radio on its left side as seen from the front, Right on its
// right. The panel turns it (MADCTL), so drawing costs nothing more, and
// touches are turned to match. The screen is square: no layout changes.
// The next frame shows it; invalidate the screen to redraw it all.
enum class Rotation : uint8_t { Normal, Left, Right };
void setRotation(Rotation r);
Rotation rotation();

// Press the screen at x, y for `ms` without a finger (the dev console uses it)
void simulateTouch(int16_t x, int16_t y, uint32_t ms = 80);
// Press at x1, y1, move in a straight line to x2, y2 over `ms`, then lift
void simulateDrag(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint32_t ms = 250);

}  // namespace display
