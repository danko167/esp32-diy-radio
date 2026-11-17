#include "player.h"

#include <Arduino.h>
#include <Audio.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include <freertos/idf_additions.h>

#include <atomic>

#include "app/storage.h"
#include "hal/codec.h"
#include "hal/pins.h"

namespace player {
namespace {

enum class Cmd : uint8_t { PlayUrl, PlayFile, Stop, Release, Reclaim };

struct Command {
    Cmd cmd;
    bool repeat;
    bool fromCard;
    char source[200];
    char name[64];
};

Audio *audio;  // nullptr while the recorder has the I2S bus, or when it couldn't start
QueueHandle_t queue;
SemaphoreHandle_t released;  // given once Release is done
SemaphoreHandle_t mutex;

// Shared with the UI, guarded by `mutex`
Status current;
uint32_t rev = 0;

// Volume skips the queue: slider drags and alarm fades send many, and while
// the task is stuck connecting they filled it, so each one stalled the UI
// 100 ms and later commands, Stop among them, were dropped. The task applies
// the latest value instead.
volatile uint8_t vol = 40;
volatile bool volumeChanged = false;

const char *volatile holdReason = nullptr;

// Only touched by the player task
bool lent = false;     // the recorder has the bus: from Release till Reclaim
uint32_t retryAt = 0;  // when the library couldn't start: when to try again
bool repeatFile = false;
bool fileOnCard = false;
bool reachedEnd = false;
// The latest play request while the recorder has the bus (an alarm's), or
// while the library couldn't start
Command waiting;
bool haveWaiting = false;
Command playing;  // the last one started, for Release to start again
// Held while the library has a card file open (see storage::CardUse)
storage::CardUse cardUse{false};

fs::FS &fileSystem() { return fileOnCard ? static_cast<fs::FS &>(SD_MMC) : LittleFS; }

// After the library let go of its file: stopSong() closes it while playing,
// and so does the end of a file. A file whose header it can't read, though,
// stops it with the file left open, closed only when it opens another; so an
// internal one (tones::begin() writes it) goes in its place first.
void releaseCard(bool stillOpen) {
    if (!cardUse) return;
    if (stillOpen && audio->connecttoFS(LittleFS, "/sounds/chime-1.wav")) audio->stopSong();
    cardUse.release();
}

struct Lock {
    Lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(mutex); }
};

void setState(State s) {
    Lock lock;
    current.state = s;
    rev++;
}

// Called from audio->loop(), so on the player task
void onInfo(Audio::msg_t m) {
    if (!m.msg) return;
    switch (m.e) {
        case Audio::evt_name: {
            Lock lock;
            if (!current.name[0]) strlcpy(current.name, m.msg, sizeof(current.name));
            rev++;
            break;
        }
        case Audio::evt_streamtitle: {
            Serial.printf("player: now playing %s\n", m.msg);
            Lock lock;
            strlcpy(current.title, m.msg, sizeof(current.title));
            rev++;
            break;
        }
        case Audio::evt_bitrate: {
            Lock lock;
            current.bitrate = strtoul(m.msg, nullptr, 10);
            rev++;
            break;
        }
        case Audio::evt_eof:
            reachedEnd = true;
            break;
        default:
            break;
    }
}

void start(const Command &c) {
    audio->stopSong();
    releaseCard(false);
    {
        Lock lock;
        current = Status{};
        current.state = State::Connecting;
        strlcpy(current.source, c.source, sizeof(current.source));
        strlcpy(current.name, c.name, sizeof(current.name));
        rev++;
    }
    playing = c;
    repeatFile = c.cmd == Cmd::PlayFile && c.repeat;
    fileOnCard = c.fromCard;
    reachedEnd = false;
    codec::setAmplifier(true);
    // Blocks while connecting; fine here, that's what this task is for. A card
    // that's gone (or going) isn't touched.
    bool ok = false;
    if (c.cmd == Cmd::PlayUrl) {
        ok = audio->connecttohost(c.source);
    } else if (!fileOnCard || cardUse.take()) {
        ok = audio->connecttoFS(fileSystem(), c.source);
    }
    if (ok) {
        Serial.printf("player: playing %s\n", c.source);
    } else {
        Serial.printf("player: couldn't play %s\n", c.source);
        releaseCard(false);  // it opens the file only when it can play it
        codec::setAmplifier(false);
    }
    setState(ok ? State::Playing : State::Failed);
}

// False when the library couldn't set up the bus: most likely no internal
// RAM for its output buffers (right after a recording, say). It would then
// "play" into a channel that never runs, so it goes, and run() tries again
// every second; play requests wait meanwhile.
bool makeAudio() {
    audio = new Audio();
    // Its output buffers are internal RAM, the scarcest there is: 12 of 256
    // frames (24 KB, 64 ms of sound) instead of the library's 16 (32 KB)
    audio->settings.DMA_DESC_NUM = 12;
    if (!audio->setPinout(PIN_I2S_BCLK, PIN_I2S_LRCK, PIN_I2S_DOUT, PIN_I2S_MCLK)) {
        if (!retryAt) Serial.println("player: couldn't start the sound chip, trying again every second");
        delete audio;
        audio = nullptr;
        retryAt = millis() + 1000;
        return false;
    }
    if (retryAt) Serial.println("player: the sound chip started after all");
    retryAt = 0;
    audio->setOutputSampleRate(Audio::SR_48000);  // the codec is set up for 48 kHz only
    audio->setVolumeSteps(100);
    audio->setVolume(vol);
    return true;
}

// After stopSong(), the library's own task still plays out the sound it
// has, and deleting the library under one of its writes would pull the bus
// from beneath it. Idle, the bus sends silence and counts every buffer it
// sends as free: once the count passes the number of buffers, nothing has
// been written for that long, so all of it has been played.
void waitTillDrained() {
    for (const uint32_t start = millis(); millis() - start < 1500; vTaskDelay(pdMS_TO_TICKS(10))) {
        if (audio->m_dmaFreeDesc.load() > audio->settings.DMA_DESC_NUM) return;
    }
    Serial.println("player: the sound didn't drain, letting go of the bus anyway");
}

void stopPlaying() {
    audio->stopSong();
    releaseCard(false);
    codec::setAmplifier(false);
    repeatFile = false;
    setState(State::Stopped);
}

void startWaiting() {
    if (!audio || !haveWaiting) return;
    haveWaiting = false;
    start(waiting);
}

void handle(const Command &c) {
    switch (c.cmd) {
        case Cmd::PlayUrl:
        case Cmd::PlayFile:
            if (audio) {
                start(c);
            } else {
                waiting = c;
                haveWaiting = true;
            }
            break;
        case Cmd::Stop:
            haveWaiting = false;
            if (audio) stopPlaying();
            break;
        case Cmd::Release:
            lent = true;
            if (audio) {
                // Asked for once the recording was called off (an alarm's,
                // queued between the hold's Stop and this): it starts again
                // when the bus is back. Anything from before the hold was
                // stopped by that Stop.
                if (!holdReason && (current.state == State::Playing || current.state == State::Connecting)) {
                    waiting = playing;
                    haveWaiting = true;
                }
                stopPlaying();
                waitTillDrained();
                delete audio;  // its I2S channel with it
                audio = nullptr;
            }
            xSemaphoreGive(released);
            break;
        case Cmd::Reclaim:
            lent = false;
            if (!audio) makeAudio();  // if it can't, run() tries again
            startWaiting();
            break;
    }
}

void run(void *) {
    Command c;
    for (;;) {
        while (xQueueReceive(queue, &c, 0) == pdTRUE) handle(c);
        if (!audio) {
            if (!lent && (int32_t)(millis() - retryAt) >= 0 && makeAudio()) startWaiting();
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (volumeChanged) {
            volumeChanged = false;  // before reading, so a newer value isn't missed
            audio->setVolume(vol);
        }
        audio->loop();

        if (reachedEnd) {
            reachedEnd = false;
            if (repeatFile) {
                // Still holding the card's use; if this fails, the branch below lets it go
                audio->connecttoFS(fileSystem(), current.source);
            } else {
                releaseCard(false);
                codec::setAmplifier(false);
                setState(State::Stopped);
            }
        } else if (current.state == State::Playing && !audio->isRunning()) {
            // A file can end without telling us (the library's WAV decoder
            // does): a repeating one (an alarm's) starts again. One that can't,
            // or a stream that dropped, is Failed, so an alarm falls back to
            // its chime; one that opens but plays nothing, the alarm notices
            // by framesPlayed() standing still.
            if (!repeatFile || !audio->connecttoFS(fileSystem(), current.source)) {
                releaseCard(true);
                codec::setAmplifier(false);
                setState(State::Failed);
            }
        }
        vTaskDelay(1);
    }
}

void send(const Command &c, TickType_t wait = pdMS_TO_TICKS(100)) { xQueueSend(queue, &c, wait); }

}  // namespace

bool begin() {
    mutex = xSemaphoreCreateMutex();
    // 2 KB, so in PSRAM: only ever used from tasks, never from an ISR, so
    // never while the cache is off
    queue = xQueueCreateWithCaps(8, sizeof(Command), MALLOC_CAP_SPIRAM);
    released = xSemaphoreCreateBinary();
    if (!codec::begin()) return false;

    Audio::audio_info_callback = onInfo;
    makeAudio();  // if it can't, run() tries again
    // Next to Wi-Fi and the decoder on core 0; the UI has core 1
    xTaskCreatePinnedToCore(run, "player", 8192, nullptr, 2, nullptr, 0);
    return true;
}

void playUrl(const char *url, const char *name) {
    if (holdReason) return;
    Command c{Cmd::PlayUrl};
    strlcpy(c.source, url, sizeof(c.source));
    strlcpy(c.name, name ? name : "", sizeof(c.name));
    send(c);
}

void playFile(const char *path, bool repeat, bool fromCard) {
    if (holdReason) return;
    Command c{Cmd::PlayFile, repeat, fromCard};
    strlcpy(c.source, path, sizeof(c.source));
    send(c);
}

void stop() { send(Command{Cmd::Stop}); }

void hold(const char *why) {
    holdReason = why;
    if (why) stop();
}

const char *held() { return holdReason; }

// No timeouts: a Release given up on but still queued would later let go
// of the bus for no one, and its answer could pass for the next one's while
// a stale Reclaim brings the library back under the recorder. The player
// always gets to it: connecting and draining both end within seconds.
void releaseI2s() {
    send(Command{Cmd::Release}, portMAX_DELAY);
    xSemaphoreTake(released, portMAX_DELAY);
}

void reclaimI2s() { send(Command{Cmd::Reclaim}, portMAX_DELAY); }

void setVolume(uint8_t percent) {
    vol = min<uint8_t>(percent, 100);
    volumeChanged = true;
}

uint8_t volume() { return vol; }

Status status() {
    Lock lock;
    return current;
}

uint32_t revision() {
    Lock lock;
    return rev;
}

extern std::atomic<uint32_t> framesSent;  // player_frames.cpp
uint32_t framesPlayed() { return framesSent.load(std::memory_order_relaxed); }

}  // namespace player
