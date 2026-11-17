#include "imu.h"

#include <Arduino.h>
#include <Wire.h>
#include <sensor/imu/qmi8658/SensorQMI8658.hpp>

namespace imu {
namespace {

// ±2 g: the finest steps (1/16384 g, well under 0.01°), and a radio standing
// still never sees more
constexpr float kLsbPerG = 16384.0f;

// How the chip sits on the board: which of its axes, and which way, is the
// screen's x (right), y (up) and z (out of the screen). Found with the dev
// console's g, the radio in a few positions: lying on its back, z reads +1 g.
struct Axis {
    uint8_t chip;  // 0, 1, 2: the chip's x, y, z
    int8_t sign;
};
constexpr Axis kScreen[3] = {{0, 1}, {1, 1}, {2, 1}};

SensorQMI8658 chip;
bool online = false;
bool running_ = false;

}  // namespace

bool begin() {
    // Waveshare wires SA0 high (0x6B); 0x6A just in case a board doesn't
    online = chip.begin(Wire, QMI8658_L_SLAVE_ADDRESS) || chip.begin(Wire, QMI8658_H_SLAVE_ADDRESS);
    if (!online) {
        Serial.println("imu: QMI8658 not found");
        return false;
    }
    // About 62 Hz, without the chip's own low-pass filter: at 2.66% of that it
    // took a second to settle. The screen smooths the readings instead.
    chip.configAccel(AccelFullScaleRange::FS_2G, 56.0f, SensorQMI8658::LpfMode::OFF);
    chip.disableAccel();
    chip.disableGyro();
    return true;
}

bool present() { return online; }

void start() {
    if (!online || running_) return;
    running_ = chip.enableAccel();
}

void stop() {
    if (!online || !running_) return;
    chip.disableAccel();
    running_ = false;
}

bool running() { return running_; }

bool readChip(float &x, float &y, float &z) {
    if (!running_) return false;
    int16_t rx, ry, rz;
    if (!chip.readAccelRaw(rx, ry, rz)) return false;
    x = rx / kLsbPerG;
    y = ry / kLsbPerG;
    z = rz / kLsbPerG;
    return true;
}

bool read(float &x, float &y, float &z) {
    float c[3];
    if (!readChip(c[0], c[1], c[2])) return false;
    x = kScreen[0].sign * c[kScreen[0].chip];
    y = kScreen[1].sign * c[kScreen[1].chip];
    z = kScreen[2].sign * c[kScreen[2].chip];
    return true;
}

}  // namespace imu
