#include "light.h"

#include <Arduino.h>
#include <Wire.h>

namespace light {
namespace {

constexpr uint8_t kAddress = 0x23;  // its ADDR pin low (0x5C when high)
constexpr uint8_t kPowerOn = 0x01;
constexpr uint8_t kContinuousHigh = 0x10;  // 1 lux steps, a reading every 120 ms
bool found = false;

bool send(uint8_t command) {
    Wire.beginTransmission(kAddress);
    Wire.write(command);
    return Wire.endTransmission() == 0;
}

}  // namespace

bool begin() {
    found = send(kPowerOn) && send(kContinuousHigh);
    if (!found) Serial.println("light: no BH1750 at 0x23");
    return found;
}

bool present() { return found; }

bool read(float &lux) {
    if (!found || Wire.requestFrom(kAddress, (uint8_t)2) != 2) return false;
    const uint16_t raw = Wire.read() << 8 | Wire.read();
    lux = raw / 1.2f;  // the datasheet's count per lux, at the default measuring time
    return true;
}

}  // namespace light
