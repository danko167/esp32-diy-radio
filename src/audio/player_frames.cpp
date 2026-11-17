// The audio library's hook for every chunk it writes to the bus, counted for
// player::framesPlayed(). In a file of its own, without Audio.h: that header
// declares the hook weak, which makes a definition after it weak too, and of
// two weak ones the linker may keep the library's empty one.

#include <stdint.h>

#include <atomic>

namespace player {
std::atomic<uint32_t> framesSent{0};  // read by framesPlayed() in player.cpp
}

// On the library's own task. 32-bit stereo: two words a frame. The silence
// the bus sends by itself never comes through here.
void audio_process_i2s(int32_t *, int16_t validSamples, bool *) {
    player::framesSent.fetch_add(validSamples / 2, std::memory_order_relaxed);
}
