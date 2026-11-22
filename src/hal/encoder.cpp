#include "encoder.h"

#include <Arduino.h>
#include <Wire.h>
#include <freertos/idf_additions.h>

#include <atomic>

namespace encoder {
namespace {

constexpr uint8_t kAddress = 0x20;
constexpr uint8_t kGpioA = 0x12;
constexpr uint8_t kGppuA = 0x0C;  // port A's pull-ups
constexpr uint8_t kBitA = 4, kBitB = 5, kBitButton = 6;
// Which way is clockwise: as wired (S1 on GPA4, S2 on GPA5) the lines turn
// the other way round, checked on the radio
constexpr int kClockwise = -1;
constexpr uint32_t kDebounceMs = 20;  // the button's state must hold this long
constexpr uint32_t kLongMs = 800;

std::atomic<int> steps{0};
std::atomic<uint8_t> presses{0}, longPresses{0};
volatile bool logOn = false;

// Straight into our own byte, in one locked transaction: this runs on its own
// task while the UI polls other chips on the bus (see mics.cpp). Wire is bus 0.
bool readPort(uint8_t &value) {
    uint8_t reg = kGpioA;
    size_t got = 0;
    return i2cWriteReadNonStop(0, kAddress, &reg, 1, &value, 1, 10, &got) == ESP_OK && got == 1;
}

// Quadrature: how the two lines change from one reading to the next, as
// (before << 2 | now) of the state A << 1 | B. A jump of two (a reading
// missed) or bounce adds nothing or cancels out.
const int8_t kDelta[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

void run(void *) {
    uint8_t port = 0xFF;
    readPort(port);
    uint8_t state = ((port >> kBitA) & 1) << 1 | ((port >> kBitB) & 1);
    int quarter = 0;  // quarter steps since the last click
    bool down = false, longSent = false;
    bool seenDown = false;  // the button's raw state, and since when
    uint32_t seenSince = 0, downSince = 0;
    for (;;) {
        vTaskDelay(1);
        if (!readPort(port)) continue;

        const uint8_t now = ((port >> kBitA) & 1) << 1 | ((port >> kBitB) & 1);
        if (now != state) {
            quarter += kDelta[state << 2 | now];
            state = now;
            // A click rests with both lines high: a click is done there
            if (now == 3) {
                if (quarter >= 2 || quarter <= -2) {
                    const int step = (quarter > 0 ? 1 : -1) * kClockwise;
                    steps += step;
                    if (logOn) Serial.printf("encoder: %+d\n", step);
                }
                quarter = 0;
            }
        }

        const bool raw = !((port >> kBitButton) & 1);  // pressed pulls it low
        const uint32_t t = millis();
        if (raw != seenDown) {
            seenDown = raw;
            seenSince = t;
        }
        if (seenDown != down && t - seenSince >= kDebounceMs) {
            down = seenDown;
            if (down) {
                downSince = t;
                longSent = false;
            } else if (!longSent) {
                presses++;
                if (logOn) Serial.println("encoder: press");
            }
        }
        if (down && !longSent && t - downSince >= kLongMs) {
            longSent = true;
            longPresses++;
            if (logOn) Serial.println("encoder: long press");
        }
    }
}

}  // namespace

bool begin() {
    uint8_t port;
    if (!readPort(port)) {
        Serial.println("encoder: no I/O expander at 0x20");
        return false;
    }
    // Its own pull-ups on the three pins: with the encoder unplugged they'd
    // float, and a floating button reads as held (a long press stops an alarm)
    Wire.beginTransmission(kAddress);
    Wire.write(kGppuA);
    Wire.write(1 << kBitA | 1 << kBitB | 1 << kBitButton);
    if (Wire.endTransmission() != 0) Serial.println("encoder: couldn't set the expander's pull-ups");
    // Its stack in PSRAM: it only talks I2C, never to internal flash
    if (xTaskCreatePinnedToCoreWithCaps(run, "encoder", 3072, nullptr, 3, nullptr, 0, MALLOC_CAP_SPIRAM) != pdPASS) {
        Serial.println("encoder: no memory for its task");
        return false;
    }
    return true;
}

int takeSteps() { return steps.exchange(0); }
bool takePress() { return presses.exchange(0) > 0; }
bool takeLongPress() { return longPresses.exchange(0) > 0; }

void setLogging(bool on) { logOn = on; }
bool logging() { return logOn; }

}  // namespace encoder
