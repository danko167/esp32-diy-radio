#include "buttons.h"

#include <Arduino.h>

#include "pins.h"
#include "power.h"

namespace buttons {
namespace {

constexpr uint32_t kDebounceMs = 25;
constexpr uint32_t kDoubleMs = 300;  // second press within this counts as a double
constexpr uint32_t kLongMs = 500;
constexpr uint32_t kRepeatMs = 150;
constexpr uint32_t kPmuPollMs = 20;

// Where each button is, seen from the front. BOOT and GPIO18 are ESP32 pins;
// PWR belongs to the AXP2101 power chip, which reports its presses over I2C.
enum class Source : uint8_t { Boot, Gpio18, Power };
const Source kLayout[3] = {Source::Boot, Source::Power, Source::Gpio18};  // Left, Middle, Right

struct State {
    bool down = false;      // debounced
    bool rawDown = false;
    uint32_t rawSince = 0;
    uint32_t downSince = 0;
    uint32_t lastRepeat = 0;
    bool longSent = false;
    bool consumed = false;  // this press already became a double
    bool pendingSingle = false;
    uint32_t releasedAt = 0;
};

State states[3];
Handler handler = nullptr;
bool (*wantsDouble)(Key) = nullptr;

bool powerDown = false;  // last state reported by the PMU's edge interrupts
bool powerTap = false;   // a whole press and release happened between two polls
uint32_t lastPmuPoll = 0;

void emit(Key key, Gesture g) {
    if (handler) handler(key, g);
}

void pollPmu() {
    if (millis() - lastPmuPoll < kPmuPollMs) return;
    lastPmuPoll = millis();
    bool pressed, released;
    power::readKeyEdges(pressed, released);
    if (!pressed && !released) return;
    if (pressed && released) {
        // Both edges since the last poll. If it was up, that's a whole quick
        // tap; replay it as one loop "down", then up. If it was down, it got
        // released and pressed again, so it's still down.
        if (!powerDown) powerTap = true;
    } else {
        powerDown = pressed;
    }
}

bool rawState(Source s) {
    switch (s) {
        case Source::Boot: return digitalRead(0) == LOW;
        case Source::Gpio18: return digitalRead(PIN_KEY) == LOW;
        case Source::Power:
            if (powerTap) {
                powerTap = false;
                return true;
            }
            return powerDown;
    }
    return false;
}

void update(Key key) {
    State &st = states[static_cast<int>(key)];
    const uint32_t now = millis();
    const bool raw = rawState(kLayout[static_cast<int>(key)]);

    if (raw != st.rawDown) {
        st.rawDown = raw;
        st.rawSince = now;
    }
    // The PMU already debounces its button
    const bool stable = kLayout[static_cast<int>(key)] == Source::Power || now - st.rawSince >= kDebounceMs;

    if (stable && raw != st.down) {
        st.down = raw;
        if (raw) {  // pressed
            st.downSince = now;
            st.longSent = false;
            st.consumed = false;
            if (st.pendingSingle && now - st.releasedAt <= kDoubleMs) {
                st.pendingSingle = false;
                st.consumed = true;
                emit(key, Gesture::Double);
            }
        } else if (!st.longSent && !st.consumed) {  // released after a short press
            if (wantsDouble && wantsDouble(key)) {
                st.pendingSingle = true;
                st.releasedAt = now;
            } else {
                emit(key, Gesture::Press);
            }
        }
    }

    if (st.down && !st.consumed) {
        if (!st.longSent && now - st.downSince >= kLongMs) {
            st.longSent = true;
            st.lastRepeat = now;
            emit(key, Gesture::Long);
        } else if (st.longSent && now - st.lastRepeat >= kRepeatMs) {
            st.lastRepeat = now;
            emit(key, Gesture::Repeat);
        }
    }

    if (st.pendingSingle && !st.down && now - st.releasedAt > kDoubleMs) {
        st.pendingSingle = false;
        emit(key, Gesture::Press);
    }
}

}  // namespace

void begin(Handler h, bool (*wants)(Key)) {
    handler = h;
    wantsDouble = wants;
    pinMode(0, INPUT_PULLUP);  // BOOT: only special while the chip starts
    pinMode(PIN_KEY, INPUT_PULLUP);
    // The PWR button's presses come from power:: (the AXP2101)
}

void loop() {
    pollPmu();
    for (int i = 0; i < 3; i++) update(static_cast<Key>(i));
}

void simulate(Key key, Gesture g) { emit(key, g); }

}  // namespace buttons
