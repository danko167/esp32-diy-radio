#pragma once

#include <stdint.h>

// The ES7210 behind the board's two microphones: an I2S slave on the bus the
// ES8311 speaker codec shares (the recorder drives it while recording, see
// audio/recorder.h). Set for 48 kHz from MCLK = 256 x 48 kHz, 16-bit Philips
// I2S: mic 1 on the left, mic 2 on the right. Wire must already be started.
namespace mics {

// Powers them up at `gainDb` (0..37: 3 dB steps to 33, then 34.5, 36, 37.5); the
// I2S clocks must be running
bool start(uint8_t gainDb);
void stop();  // all off: the chip's analog part draws a few mA

}  // namespace mics
