#pragma once

#include <Arduino.h>

#include <vector>

// The Recorder app's engine: the board's two mics to WAV files in /recordings
// on the microSD card (design/src/screens/Recorder.tsx). While it records it
// has the I2S bus (the player gives it up, see player::releaseI2s) and the
// radio is held off; the upload server and recording exclude each other, and
// an alarm stops a recording. The work is done by two tasks of its own: one
// takes the sound off the bus into a buffer in PSRAM, the other writes it to
// the card, whose writes can stall far longer than the bus can wait.
namespace recorder {

// Voice: 16 kHz mono (32 KB/s). Music: 48 kHz stereo (192 KB/s).
enum class Quality : uint8_t { Voice, Music };
uint32_t bytesPerSecond(Quality q);
const char *qualityName(Quality q);    // "Voice"
const char *qualityDetail(Quality q);  // "16 kHz mono"

// Starts one; false when it can't (why() says), e.g. without a card
bool start(Quality q);
// Finishes the file in the background; `why`, if given, shows on Ready
void stop(const char *why = nullptr);
void loop();  // call often, from the UI task

bool recording();  // from start() until the file is closed
Quality quality();
uint32_t seconds();  // of the one being recorded
uint32_t bytes();
uint8_t level();     // 0..100: the loudest of the last 100 ms, on a 60 dB scale
// Why the last start failed, or the last recording stopped by itself; till the next start
const char *why();

struct Recording {
    String name;  // the file's, in /recordings
    uint32_t bytes = 0;
    uint32_t seconds = 0;
    Quality quality = Quality::Voice;
};
// Newest first; reads the card (any task)
std::vector<Recording> list();
bool remove(const String &name);  // false for a name that isn't one of them
String path(const String &name);
bool isName(const String &name);  // what list() gives: no paths, no tricks
uint64_t cardFree();  // bytes, 0 without a card

uint32_t revision();  // changes with any of the above

}  // namespace recorder
