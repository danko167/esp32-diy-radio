#pragma once

// The BH1750 ambient light sensor (0x23 on the shared I2C bus, e.g. through
// the MCP23017 board's second uSup connector). Measuring all the time, about
// 8 times a second, to 1 lux. Wire must already be started; UI task only.
namespace light {

bool begin();  // false, and nothing runs, without the sensor
bool present();

// The light falling on it, in lux (a dark room: under 10; indoors: 100-500;
// daylight by a window: thousands). False without the sensor or when a read fails.
bool read(float &lux);

}  // namespace light
