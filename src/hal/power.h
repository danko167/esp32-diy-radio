#pragma once

#include <math.h>
#include <stdint.h>

// The AXP2101 power chip: the battery, charging, and the PWR button (whose
// presses it reports through interrupt flags). Wire must already be started.
namespace power {

bool begin();
void loop();  // refreshes the battery reading every few seconds

// How much is left on battery: always Ok on USB. Low and Critical come from
// the voltage, only after three readings in a row (15 s), so one dip while
// the speaker thumps doesn't count; Ok again needs a little more than Low.
enum class Level : uint8_t { Ok, Low, Critical };

struct Battery {
    bool present = false;
    bool charging = false;
    bool usb = false;     // power coming in over USB
    int percent = -1;     // 0..100, -1 while unknown: the gauge's, scaled to what it read when last full
    int gauge = -1;       // the chip's own estimate, before scaling (it counts 4.2 V as full)
    uint16_t millivolts = 0;
    Level level = Level::Ok;
};

const Battery &battery();
// What the gauge read when charging last finished: shown as 100% (100 until it's been seen)
int fullAt();
uint32_t revision();  // changes whenever battery() does, but for the millivolts

// Everything off but the chip's clock; the PWR button starts the radio again.
// Does nothing while pretend() is on, so the dev console can't switch it off.
void shutdown();

// Dev console: the battery reads `mv`, on battery (no USB), from now on (a
// level still takes three readings); 0 goes back to the real readings
void pretend(uint16_t mv);
uint16_t pretending();  // pretend()'s mV, 0 when real

// How the chip charges, as set in its registers. begin() sets the power-off
// voltage, the charge current and target, and the USB limit. The end of charge
// survives restarts while a battery is connected, so it may still be the
// factory demo's. 0 = unknown.
struct ChargeSettings {
    uint16_t targetMv = 0;    // charges up to this
    uint16_t currentMa = 0;   // at this current, until near the target
    uint16_t stopMa = 0;      // then stops once the current falls to this...
    bool stopOn = false;      // ...if this is on
    uint16_t cutoffMv = 0;    // turns everything off below this
    uint16_t usbLimitMa = 0;  // most it draws from USB
};
ChargeSettings chargeSettings();  // reads the chip over I2C: on demand, not every frame

// The chip's other readings (Settings > About), updated with the battery's
struct Readings {
    uint16_t usbMv = 0;  // on the USB-C input, 0 when nothing's plugged in
    float chipC = NAN;   // the chip's own temperature, NAN until read
};
const Readings &readings();

// PWR button edges since the last call
void readKeyEdges(bool &pressed, bool &released);

}  // namespace power
