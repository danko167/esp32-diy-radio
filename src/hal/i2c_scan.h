#pragma once

// Dev console: lists what answers on the onboard I2C bus (GPIO15 SDA, GPIO14
// SCL), which external modules share with the board's own chips. Wire must
// already be started.
namespace i2c_scan {

// At 100 kHz for the scan, then back to the bus's usual 400 kHz. An MCP23017
// found at 0x20..0x27 also gets a few of its registers read (never written).
void run();

}  // namespace i2c_scan
