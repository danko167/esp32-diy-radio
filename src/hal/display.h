#pragma once

#include <lvgl.h>

namespace display {

// Brings up the AMOLED, touch and LVGL. Wire must already be started.
bool begin();

// 0..255. AMOLED brightness is set in the panel itself, there is no backlight.
void setBrightness(uint8_t level);
uint8_t brightness();

// Press the screen at x, y for `ms` without a finger (the dev console uses it)
void simulateTouch(int16_t x, int16_t y, uint32_t ms = 80);
// Press at x1, y1, move in a straight line to x2, y2 over `ms`, then lift
void simulateDrag(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint32_t ms = 250);

}  // namespace display
