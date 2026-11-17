#pragma once

#include <stdint.h>

// The board's three buttons, turned into gestures. What a gesture does is up
// to the UI, depending on the screen.
namespace buttons {

enum class Key : uint8_t { Left, Middle, Right };

enum class Gesture : uint8_t {
    Press,   // short press and release
    Double,  // two quick presses (only detected where wantsDouble() says so)
    Long,    // held for half a second
    Repeat,  // still held: repeats after Long, for things like volume
};

using Handler = void (*)(Key, Gesture);

// `wantsDouble` is asked when a key is released: if a double press means
// nothing right now, the single press fires at once instead of waiting.
// power::begin() must have run, for the PWR button.
void begin(Handler handler, bool (*wantsDouble)(Key));
void loop();  // call often

// Act as if `key` did `g` (the dev console uses it)
void simulate(Key key, Gesture g);

}  // namespace buttons
