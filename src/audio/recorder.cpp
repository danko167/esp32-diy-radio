#include "recorder.h"

#include <SD_MMC.h>
#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#include <math.h>
#include <time.h>

#include <algorithm>
#include <atomic>

#include "app/alarms.h"
#include "app/storage.h"
#include "audio/player.h"
#include "hal/mics.h"
#include "hal/pins.h"
#include "net/upload_server.h"

namespace recorder {
namespace {

constexpr char kFolder[] = "/recordings";
// On the bus it's always 48 kHz stereo, as the speaker codec is clocked for:
// Voice is made from it here
constexpr uint32_t kBusRate = 48000;
// The mics' own amplifier at its most (37.5 dB), then 4x (12 dB) here: at
// 30 dB alone, recordings played back far quieter than the radio
constexpr uint8_t kGainDb = 37;
constexpr int kBoost = 4;
constexpr size_t kFrames = 480;             // read off the bus at a time: 10 ms
constexpr size_t kRingBytes = 1024 * 1024;  // 5 s of Music, far more than a card stalls for
constexpr size_t kChunk = 32 * 1024;        // written to the card at a time
constexpr size_t kHeader = 44;
constexpr uint32_t kHeaderEveryMs = 10000;  // the header's sizes, so a power cut loses at most this
constexpr uint64_t kCardReserve = 16ull << 20;           // left free on the card
constexpr uint32_t kMaxData = 0xFFFFFFFFu - (16u << 20);  // FAT and WAV both end at 4 GB

enum class Phase : uint8_t { Idle, Running, Done };

volatile Phase phase = Phase::Idle;  // Done: both tasks have finished, loop() cleans up
volatile bool stopRequested = false;
const char *volatile reason = nullptr;
Quality current = Quality::Voice;
String currentName;
volatile uint8_t meter = 0;
std::atomic<uint32_t> rev{0};

// Filled by the capture task at `head`, emptied by the writer at `tail`: byte
// counts since the start, so head - tail is what's waiting (it wraps fine,
// as the ring's size divides 2^32)
uint8_t *ring = nullptr;
std::atomic<uint32_t> head{0}, tail{0};
std::atomic<uint32_t> dropped{0};  // didn't fit: the card fell that far behind

i2s_chan_handle_t rx = nullptr;
TaskHandle_t captureTask = nullptr, writerTask = nullptr;
volatile bool captureDone = true;

bool openBus() {
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = 8;  // 80 ms: for whatever else runs on the core meanwhile
    chan.dma_frame_num = kFrames;
    if (i2s_new_channel(&chan, nullptr, &rx) != ESP_OK) return false;
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kBusRate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg =
            {
                .mclk = (gpio_num_t)PIN_I2S_MCLK,
                .bclk = (gpio_num_t)PIN_I2S_BCLK,
                .ws = (gpio_num_t)PIN_I2S_LRCK,
                .dout = I2S_GPIO_UNUSED,
                .din = (gpio_num_t)PIN_I2S_DIN,
                .invert_flags = {},
            },
    };
    std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    if (i2s_channel_init_std_mode(rx, &std) == ESP_OK && i2s_channel_enable(rx) == ESP_OK) return true;
    i2s_del_channel(rx);
    rx = nullptr;
    return false;
}

void closeBus() {
    if (!rx) return;
    i2s_channel_disable(rx);
    i2s_del_channel(rx);
    rx = nullptr;
}

void push(const uint8_t *data, size_t length) {
    if (length > kRingBytes - (head - tail)) {
        dropped += length;
        return;
    }
    const size_t at = head % kRingBytes, first = std::min(length, kRingBytes - at);
    memcpy(ring + at, data, first);
    memcpy(ring, data + first, length - first);
    head += length;  // after the bytes: the writer only reads what this counts
}

// A sample as it goes in the file: clipped rather than wrapped round, and
// counted for the level meter
int16_t loud(int v, int &peak) {
    v = std::clamp(v, -32768, 32767);
    peak = std::max(peak, abs(v));
    return v;
}

// The capture task: the bus into the ring, as Voice or Music
void capture(void *) {
    // In PSRAM: internal RAM is scarce, and reading the bus is a copy anyway
    auto *frames = static_cast<int16_t *>(heap_caps_malloc(kFrames * 4 * 2, MALLOC_CAP_SPIRAM));  // left, right
    int16_t *out = frames + kFrames * 2;
    int32_t sum = 0;  // Voice: 3 frames of both mics make one sample
    int summed = 0;
    int peak = 0, peakFrames = 0;
    // The mics thump as they power up (about 50 ms): not in the recording
    size_t settling = kBusRate * 15 / 100;
    while (!stopRequested) {
        size_t got = 0;
        if (!frames || i2s_channel_read(rx, frames, kFrames * 4, &got, 100) != ESP_OK) {
            if (!frames) vTaskDelay(pdMS_TO_TICKS(100));  // never: 4 KB of 8 MB. Stopping still works.
            continue;
        }
        const size_t n = got / 4;
        if (settling) {
            settling -= std::min(settling, n);
            continue;
        }
        size_t length = 0;
        for (size_t i = 0; i < n; i++) {
            const int l = frames[2 * i] * kBoost, r = frames[2 * i + 1] * kBoost;
            if (current == Quality::Music) {
                out[length++] = loud(l, peak);
                out[length++] = loud(r, peak);
            } else {
                sum += l + r;
                if (++summed == 3) {
                    out[length++] = loud(sum / 6, peak);
                    sum = summed = 0;
                }
            }
        }
        push(reinterpret_cast<uint8_t *>(out), length * 2);
        peakFrames += n;
        if (peakFrames >= (int)kBusRate / 10) {
            const float db = peak ? 20 * log10f(peak / 32767.0f) : -60;
            meter = (uint8_t)std::clamp((db + 60) * 100 / 60, 0.0f, 100.0f);
            peak = peakFrames = 0;
        }
    }
    heap_caps_free(frames);
    captureDone = true;
    vTaskSuspend(nullptr);  // a PSRAM stack: loop() deletes it
}

void put16(uint8_t *p, uint16_t v) {
    p[0] = v;
    p[1] = v >> 8;
}

void put32(uint8_t *p, uint32_t v) {
    put16(p, v);
    put16(p + 2, v >> 16);
}

bool writeHeader(File &f, uint32_t data) {
    const uint16_t channels = current == Quality::Music ? 2 : 1;
    const uint32_t rate = current == Quality::Music ? 48000 : 16000;
    uint8_t h[kHeader];
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36 + data);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    put16(h + 20, 1);  // PCM
    put16(h + 22, channels);
    put32(h + 24, rate);
    put32(h + 28, rate * channels * 2);
    put16(h + 32, channels * 2);
    put16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    put32(h + 40, data);
    return f.seek(0) && f.write(h, kHeader) == kHeader && f.seek(kHeader + data);
}

// "20261103-154412-voice.wav", from the local time
String newName() {
    const time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    char name[32];
    snprintf(name, sizeof(name), "%04d%02d%02d-%02d%02d%02d-%s.wav", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
             t.tm_hour, t.tm_min, t.tm_sec, current == Quality::Music ? "music" : "voice");
    return name;
}

// The writer task: takes the bus, starts the mics and the capture, writes
// the ring to the card till stopped, then gives it all back
void writer(void *arg) {
    const uint64_t room = (uint64_t)(uintptr_t)arg << 20;  // free on the card at the start, given in MB
    const uint64_t limit = std::min<uint64_t>(room > kCardReserve ? room - kCardReserve : 0, kMaxData);
    const char *fail = nullptr;
    storage::CardUse use{false};
    File file;
    uint32_t data = 0;
    bool micsOn = false;

    // Stopped before it got going (an alarm right away, maybe while the
    // speaker was still letting go): the bus isn't taken, nothing recorded
    bool calledOff = stopRequested;
    if (!calledOff) {
        player::releaseI2s();
        calledOff = stopRequested;
    }
    if (calledOff) {
        // straight to the end below
    } else if (!openBus()) {
        fail = "Couldn't start the sound chip";
    } else if (!(micsOn = mics::start(kGainDb))) {
        fail = "The microphones didn't answer";
    } else if (!use.take()) {
        fail = "Put a microSD card in: recordings go there";
    } else {
        SD_MMC.mkdir(kFolder);
        file = SD_MMC.open(path(currentName), FILE_WRITE);
        if (!file || !writeHeader(file, 0)) fail = "Couldn't write to the card";
    }

    if (!fail && !calledOff) {
        captureDone = false;
        if (xTaskCreatePinnedToCoreWithCaps(capture, "rec-in", 4096, nullptr, 4, &captureTask, 0,
                                            MALLOC_CAP_SPIRAM) != pdPASS) {
            captureDone = true;
            captureTask = nullptr;
            fail = "Out of memory";
        }
    }

    // Till the capture ends (stopped, or the card's trouble below) and all it took is written
    uint32_t headerAt = millis();
    while (!fail && !calledOff) {
        const bool last = captureDone;  // before looking at the ring: what it put in before ending is there
        const uint32_t waiting = head - tail;
        if (waiting >= kChunk || (last && waiting)) {
            const size_t at = tail % kRingBytes;
            const size_t n = std::min<size_t>({waiting, kChunk, kRingBytes - at});
            if (file.write(ring + at, n) != n) {
                fail = "Couldn't write to the card";
                break;
            }
            tail += n;
            data += n;
            if (data + kChunk > limit && !stopRequested) {
                reason = data + kChunk > kMaxData ? "Stopped at 4 GB, the most a file can hold" : "Stopped: the card is full";
                stopRequested = true;
            }
        } else if (last) {
            break;
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (millis() - headerAt >= kHeaderEveryMs) {
            headerAt = millis();
            if (writeHeader(file, data)) file.flush();
        }
    }

    stopRequested = true;
    while (!captureDone) vTaskDelay(pdMS_TO_TICKS(10));
    if (file) {
        writeHeader(file, data);
        file.close();
        if (!data) SD_MMC.remove(path(currentName));  // nothing in it
    }
    use.release();
    if (micsOn) mics::stop();
    closeBus();
    player::reclaimI2s();
    player::hold(nullptr);
    if (fail) {
        reason = fail;
        Serial.printf("recorder: %s\n", fail);
    }
    Serial.printf("recorder: %s, %lu bytes, %lu dropped\n", currentName.c_str(), (unsigned long)data,
                  (unsigned long)dropped);
    phase = Phase::Done;
    vTaskSuspend(nullptr);
}

}  // namespace

uint32_t bytesPerSecond(Quality q) { return q == Quality::Music ? 192000 : 32000; }
const char *qualityName(Quality q) { return q == Quality::Music ? "Music" : "Voice"; }
const char *qualityDetail(Quality q) { return q == Quality::Music ? "48 kHz stereo" : "16 kHz mono"; }

bool start(Quality q) {
    if (phase != Phase::Idle) return false;
    reason = nullptr;
    rev++;
    if (upload_server::running()) {
        reason = "Not while the upload page is on";
        return false;
    }
    // It would silence the alarm, and stop again for it anyway
    if (alarms::ringing()) {
        reason = "Not while an alarm is ringing";
        return false;
    }
    if (!storage::checkForCard()) {
        reason = "Put a microSD card in: recordings go there";
        return false;
    }
    const uint64_t free = cardFree();
    if (free < kCardReserve + kChunk * 4) {
        reason = "The card is full";
        return false;
    }
    ring = static_cast<uint8_t *>(heap_caps_malloc(kRingBytes, MALLOC_CAP_SPIRAM));
    if (!ring) {
        reason = "Out of memory";
        return false;
    }
    current = q;
    currentName = newName();
    head = tail = dropped = 0;
    meter = 0;
    stopRequested = false;
    phase = Phase::Running;
    // The radio stops for it: the speaker would be in the recording
    player::hold("Off while recording");
    // Its stack in PSRAM, like the upload server's: it only writes to the card
    if (xTaskCreatePinnedToCoreWithCaps(writer, "rec", 6144, (void *)(uintptr_t)(free >> 20), 1, &writerTask, 0,
                                        MALLOC_CAP_SPIRAM) != pdPASS) {
        writerTask = nullptr;
        heap_caps_free(ring);
        ring = nullptr;
        phase = Phase::Idle;
        player::hold(nullptr);
        reason = "Out of memory";
        return false;
    }
    Serial.printf("recorder: recording %s\n", currentName.c_str());
    return true;
}

// Also when it's already stopping, by itself (the card full) or by an earlier
// call: the hold lasts till the writer is done, and would drop the sound
// that may start right away (an alarm). The player keeps that till it has
// the bus back.
void stop(const char *why) {
    if (phase != Phase::Running) return;
    if (why) reason = why;
    stopRequested = true;
    player::hold(nullptr);
    rev++;
}

void loop() {
    if (phase != Phase::Done) return;
    if (captureTask) vTaskDeleteWithCaps(captureTask);
    vTaskDeleteWithCaps(writerTask);
    captureTask = writerTask = nullptr;
    heap_caps_free(ring);
    ring = nullptr;
    meter = 0;
    phase = Phase::Idle;
    rev++;
}

bool recording() { return phase != Phase::Idle; }
Quality quality() { return current; }
uint32_t seconds() { return head / bytesPerSecond(current); }
uint32_t bytes() { return head; }
uint8_t level() { return meter; }
const char *why() { return reason; }

String path(const String &name) { return String(kFolder) + "/" + name; }

bool isName(const String &name) {
    // 20261103-154412-voice.wav
    if (name.length() != 25 || !(name.endsWith("-voice.wav") || name.endsWith("-music.wav"))) return false;
    for (int i = 0; i < 15; i++) {
        if (i == 8 ? name[i] != '-' : !isdigit((unsigned char)name[i])) return false;
    }
    return true;
}

std::vector<Recording> list() {
    std::vector<Recording> out;
    storage::CardUse use;
    if (!use) return out;
    File dir = SD_MMC.open(kFolder);
    if (!dir || !dir.isDirectory()) return out;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        const String name = f.name();
        if (f.isDirectory() || !isName(name) || f.size() < kHeader) continue;
        Recording r;
        r.name = name;
        r.bytes = f.size();
        r.quality = name.endsWith("-music.wav") ? Quality::Music : Quality::Voice;
        r.seconds = (r.bytes - kHeader) / bytesPerSecond(r.quality);
        out.push_back(r);
    }
    std::sort(out.begin(), out.end(), [](const Recording &a, const Recording &b) { return a.name > b.name; });
    return out;
}

bool remove(const String &name) {
    if (!isName(name) || (phase != Phase::Idle && name == currentName)) return false;
    storage::CardUse use;
    if (!use || !SD_MMC.remove(path(name))) return false;
    rev++;
    return true;
}

uint64_t cardFree() {
    storage::CardUse use;
    return use ? SD_MMC.totalBytes() - SD_MMC.usedBytes() : 0;
}

uint32_t revision() { return rev; }

}  // namespace recorder
