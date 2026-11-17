// Register sequence ported from Espressif's es7210 driver (esp_codec_dev), on
// top of Wire like codec.cpp, for two mics and one sample rate

#include "mics.h"

#include <Arduino.h>
#include <Wire.h>

namespace mics {
namespace {

constexpr uint8_t kAddress = 0x40;

bool write(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(kAddress);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

// Straight into our own byte, in one locked transaction: this runs on the
// recorder's task while the UI polls other chips on the bus, and Wire lets
// go of its lock after requestFrom(), so Wire.read() could return theirs.
// Wire is bus 0.
uint8_t read(uint8_t reg) {
    uint8_t value = 0;
    size_t got = 0;
    if (i2cWriteReadNonStop(0, kAddress, &reg, 1, &value, 1, 50, &got) != ESP_OK || got != 1) return 0;
    return value;
}

// Mics 1 and 2 on, 3 and 4 off; `gain` is the register's 0..14 (3 dB steps,
// then 1.5 from 33 dB)
void selectMics(uint8_t gain) {
    for (uint8_t reg = 0x43; reg <= 0x46; reg++) write(reg, read(reg) & ~0x10);
    write(0x4B, 0xFF);  // mic 1/2 power off, then on below
    write(0x4C, 0xFF);  // mic 3/4 power off
    write(0x01, read(0x01) & ~0x0B);  // the clocks of ADC 1/2
    write(0x4B, 0x00);
    write(0x43, 0x10 | gain);
    write(0x44, 0x10 | gain);
    write(0x12, 0x00);  // two mics: plain I2S, no TDM
}

}  // namespace

bool start(uint8_t gainDb) {
    const uint8_t gain = gainDb >= 33 ? min(11 + ((gainDb - 33) * 2 + 1) / 3, 14) : gainDb / 3;

    // Reset, then the clocks of everything but the ADCs on
    if (!write(0x00, 0xFF)) {
        Serial.println("mics: ES7210 not found");
        return false;
    }
    write(0x00, 0x32);
    write(0x01, 0x3F);
    write(0x09, 0x30);  // chip state cycle
    write(0x0A, 0x30);  // power on state cycle
    // High-pass filters: no DC offset in the recording
    write(0x23, 0x2A);
    write(0x22, 0x0A);
    write(0x20, 0x0A);
    write(0x21, 0x2A);
    write(0x08, read(0x08) & ~0x01);  // slave
    write(0x40, 0x43);  // analog on, VDDA 3.3 V, VMID 5 kOhm start
    write(0x41, 0x70);  // mic 1/2 bias 2.87 V
    write(0x42, 0x70);  // mic 3/4 bias 2.87 V
    // MCLK 12.288 MHz, LRCK 48 kHz: ADC divider 1, doubler and DLL on, OSR 32, LRCK / 256
    write(0x07, 0x20);
    write(0x02, 0xC1);
    write(0x04, 0x01);
    write(0x05, 0x00);
    write(0x11, 0x60);  // 16-bit, I2S
    selectMics(gain);

    // Start: what esp_codec_dev's enable does
    const uint8_t clocks = read(0x01);
    write(0x01, clocks);
    write(0x06, 0x00);  // power down off
    write(0x40, 0x43);
    for (uint8_t reg = 0x47; reg <= 0x4A; reg++) write(reg, 0x08);
    selectMics(gain);
    write(0x40, 0x43);
    write(0x00, 0x71);
    write(0x00, 0x41);
    return true;
}

void stop() {
    for (uint8_t reg = 0x47; reg <= 0x4A; reg++) write(reg, 0xFF);
    write(0x4B, 0xFF);
    write(0x4C, 0xFF);
    write(0x40, 0xC0);
    write(0x01, 0x7F);
    write(0x06, 0x07);
}

}  // namespace mics
