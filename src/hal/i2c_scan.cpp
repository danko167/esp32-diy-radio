#include "i2c_scan.h"

#include <Arduino.h>
#include <Wire.h>

#include "pins.h"

namespace i2c_scan {
namespace {

constexpr uint32_t kScanHz = 100000;
constexpr uint32_t kBusHz = 400000;  // what main.cpp starts the bus at

// The board's own chips, to tell them from what's been added
const char *knownName(uint8_t address) {
    switch (address) {
        case 0x18: return "ES8311 speaker codec";
        case 0x34: return "AXP2101 power chip";
        case 0x40: return "ES7210 mics";
        case 0x51: return "PCF85063 clock";
        case 0x5A: return "CST9220 touch";
        case 0x6B: return "QMI8658 motion sensor";
        default: return address >= 0x20 && address <= 0x27 ? "MCP23017 range" : "unknown";
    }
}

bool readRegister(uint8_t address, uint8_t reg, uint8_t &value) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(address, (uint8_t)1) != 1) return false;
    value = Wire.read();
    return true;
}

// Reads only. With IOCON.BANK = 0 (its power-on value), IODIRA/B start as
// 0xFF (all inputs) and IOCON as 0x00, so a chip that answers these as
// expected is talking properly; GPIOA/B are whatever is on the pins.
void checkMcp23017(uint8_t address) {
    const struct {
        uint8_t reg;
        const char *name;
        int expected;  // -1: any
    } regs[] = {{0x00, "IODIRA", 0xFF}, {0x01, "IODIRB", 0xFF}, {0x0A, "IOCON", 0x00},
                {0x12, "GPIOA", -1},    {0x13, "GPIOB", -1}};
    Serial.printf("i2c: MCP23017 at 0x%02X, reading registers:\n", address);
    bool asExpected = true;
    for (const auto &r : regs) {
        uint8_t value = 0;
        if (!readRegister(address, r.reg, value)) {
            Serial.printf("i2c:   %-6s (0x%02X) no answer\n", r.name, r.reg);
            asExpected = false;
            continue;
        }
        const bool ok = r.expected < 0 || value == r.expected;
        asExpected &= ok;
        Serial.printf("i2c:   %-6s (0x%02X) = 0x%02X%s\n", r.name, r.reg, value,
                      r.expected < 0 ? "" : ok ? "  (as at power-on)" : "  (not the power-on value)");
    }
    Serial.printf("i2c: MCP23017 %s\n", asExpected ? "answers as expected" : "answers, but not as expected");
}

}  // namespace

void run() {
    Wire.setClock(kScanHz);
    Serial.printf("i2c: scanning SDA %d, SCL %d at %lu kHz\n", PIN_I2C_SDA, PIN_I2C_SCL,
                  (unsigned long)(kScanHz / 1000));
    int found = 0;
    uint8_t mcp = 0;
    for (uint8_t address = 0x08; address <= 0x77; address++) {
        Wire.beginTransmission(address);
        const uint8_t result = Wire.endTransmission();
        if (result == 0) {
            Serial.printf("i2c:   0x%02X  %s\n", address, knownName(address));
            found++;
            if (address >= 0x20 && address <= 0x27 && !mcp) mcp = address;
        } else if (result != 2) {  // 2: no answer at this address, the usual
            Serial.printf("i2c:   0x%02X  bus error %u\n", address, result);
        }
    }
    Serial.printf("i2c: %d device%s found\n", found, found == 1 ? "" : "s");
    if (mcp) {
        checkMcp23017(mcp);
    } else {
        Serial.println("i2c: nothing at 0x20..0x27, no MCP23017");
    }
    Wire.setClock(kBusHz);
}

}  // namespace i2c_scan
