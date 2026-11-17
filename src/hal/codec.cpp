// Register sequence ported from Espressif's es8311 driver (as shipped in
// Waveshare's examples), on top of Wire: the original uses ESP-IDF's legacy
// I2C driver, which can't coexist with Arduino's.

#include "codec.h"

#include <Arduino.h>
#include <Wire.h>

#include "pins.h"

namespace codec {
namespace {

constexpr uint8_t kAddress = 0x18;

bool write(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(kAddress);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

uint8_t read(uint8_t reg) {
    Wire.beginTransmission(kAddress);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(kAddress, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0;
}

}  // namespace

bool begin() {
    pinMode(PIN_SPK_PA, OUTPUT);
    setAmplifier(false);

    // Reset, then power on as I2S slave
    if (!write(0x00, 0x1F)) {
        Serial.println("codec: ES8311 not found");
        return false;
    }
    write(0x00, 0x00);
    write(0x00, 0x80);

    // Clocks: all on, MCLK from the MCLK pin. Dividers for 12.288 MHz / 48 kHz:
    // pre_div 1, pre_multi 1x, adc/dac div 1, single speed, LRCK 256, BCLK div 4, OSR 0x10
    write(0x01, 0x3F);
    write(0x02, read(0x02) & 0x07);
    write(0x03, 0x10);
    write(0x04, 0x10);
    write(0x05, 0x00);
    write(0x06, (read(0x06) & 0xC0) | 0x03);  // also SCLK not inverted
    write(0x07, read(0x07) & 0xC0);
    write(0x08, 0xFF);

    // Serial port: slave, I2S, 32-bit in and out
    write(0x00, read(0x00) & 0xBF);
    write(0x09, 4 << 2);
    write(0x0A, 4 << 2);

    write(0x0D, 0x01);  // power up analog
    write(0x0E, 0x02);  // analog PGA, ADC modulator
    write(0x12, 0x00);  // power up DAC
    write(0x13, 0x10);  // output to the headphone/line driver
    write(0x1C, 0x6A);  // ADC: bypass EQ, cancel DC offset
    write(0x37, 0x08);  // bypass DAC EQ

    setVolume(80);
    return true;
}

void setVolume(uint8_t percent) {
    percent = min<uint8_t>(percent, 100);
    write(0x32, percent == 0 ? 0 : percent * 256 / 100 - 1);
}

void setAmplifier(bool on) { digitalWrite(PIN_SPK_PA, on ? HIGH : LOW); }

}  // namespace codec
