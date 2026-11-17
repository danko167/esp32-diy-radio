#pragma once

#include <stdint.h>

// Audio playback service. It owns the ESP32-audioI2S decoder in its own task,
// so slow network calls never block the UI. Every function here only queues a
// command or copies state, so it's safe and quick to call from the UI.
namespace player {

enum class State : uint8_t { Stopped, Connecting, Playing, Failed };

struct Status {
    State state = State::Stopped;
    char source[200] = "";  // URL or file path being played
    char name[64] = "";     // station name: ours, or the stream's own
    char title[128] = "";   // "Artist - Song" from the stream's metadata
    uint32_t bitrate = 0;   // bits per second, 0 when unknown
};

bool begin();  // Wire and LittleFS must be started

void playUrl(const char *url, const char *name);
void playFile(const char *path, bool repeat, bool fromCard = false);  // internal flash or microSD
void stop();

// Nothing starts playing while held (the upload server is on): play requests
// are dropped and `why` is what the screens show. nullptr lets go again.
void hold(const char *why);
const char *held();  // the reason, nullptr when not held

// The I2S bus for the recorder, which has the mics on it: the decoder goes,
// and with it its DMA buffers (24 KB of internal RAM). Waits until it's done,
// however long the player takes to get to it (a few seconds when it's
// connecting). Play requests meanwhile wait (the latest one), and start once
// reclaimI2s() has the decoder back. One recorder at a time, each release
// followed by one reclaim.
void releaseI2s();
void reclaimI2s();

void setVolume(uint8_t percent);  // 0..100
uint8_t volume();

Status status();
uint32_t revision();  // changes whenever status() would return something new

// Grows while sound goes out to the speaker: 48 kHz frames the decoder has
// written to the bus, not the silence the bus sends by itself. Wraps; compare
// two readings.
uint32_t framesPlayed();

}  // namespace player
