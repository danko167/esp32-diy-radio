#pragma once

#include <stdint.h>

// Onboard ES8311 audio codec and the speaker amplifier behind it.
// Configured for what the audio player sends: 48 kHz, 32-bit Philips I2S
// with MCLK = 256 x 48 kHz. Wire must already be started.
namespace codec {

bool begin();
void setVolume(uint8_t percent);  // codec output level, 0..100
void setAmplifier(bool on);       // off while silent: no hiss, less power

}  // namespace codec
