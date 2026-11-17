#pragma once

// The onboard QMI8658 motion sensor, used as a spirit level (the Level app).
// Only its accelerometer, and only while something wants it: it sleeps
// otherwise. Wire must already be started.
namespace imu {

bool begin();    // finds the chip and puts it to sleep; false if it isn't there
bool present();

void start();  // the accelerometer on (the Level opens)
void stop();   // and off again
bool running();

// Which way is up, in g, along the screen's axes as you look at it: x to the
// right, y up, z out of the screen towards you. (At rest an accelerometer
// feels the table pushing up, 1 g.) Upright on a level table: about (0, 1, 0);
// lying on its back: (0, 0, 1). False when it isn't started or the read failed.
bool read(float &x, float &y, float &z);

// The raw axes of the chip, for finding how it sits on the board (the dev
// console's g)
bool readChip(float &x, float &y, float &z);

}  // namespace imu
