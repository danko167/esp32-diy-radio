#include "usb_update.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <Update.h>
#include <esp_heap_caps.h>

#include "app/storage.h"
#include "app/watchdog.h"
#include "audio/player.h"

namespace usb_update {
namespace {

// Must match tools/usb_update.py. The USB serial drops what doesn't fit its
// receive buffer, so the laptop waits for "#ACK" after each chunk. The ack
// goes out before the chunk is written, and the buffer holds two chunks, so
// the next one arrives while the flash is busy.
constexpr size_t kChunk = 4096;
constexpr size_t kRxBuffer = 2 * kChunk + 256;
constexpr size_t kNormalRxBuffer = 256;  // the core's default
constexpr uint32_t kTimeoutMs = 5000;

// No Serial.flush(): with the send timeout at 0 (main.cpp, so logging never
// stalls the UI) it doesn't wait, it throws away what hasn't gone out yet
void reply(const char *line) { Serial.println(line); }

// Reads `size` bytes in acknowledged chunks, handing each to `sink` (false
// stops it). `progress` gets 0..100 meanwhile.
bool transfer(size_t size, bool (*sink)(const uint8_t *, size_t), void (*progress)(int)) {
    // In PSRAM and only meanwhile, not 4 KB of internal RAM for good. Both
    // sinks copy out of it before flash or DMA sees it (Update into its own
    // buffer, the card driver through a DMA-capable one).
    uint8_t *buf = (uint8_t *)heap_caps_malloc(kChunk, MALLOC_CAP_SPIRAM);
    if (!buf) {
        reply("#FAIL no memory");
        return false;
    }
    Serial.setRxBufferSize(kRxBuffer);
    reply("#READY");
    if (progress) progress(0);
    size_t received = 0;
    int shown = 0;
    bool ok = true;
    while (ok && received < size) {
        const size_t want = min(kChunk, size - received);
        size_t got = 0;
        const uint32_t start = millis();
        while (got < want) {
            watchdog::feed();  // the whole transfer takes far longer, and a chunk up to kTimeoutMs
            got += Serial.read(buf + got, want - got);
            if (millis() - start > kTimeoutMs) {
                reply("#FAIL timed out");
                ok = false;
                break;
            }
            if (got < want) delay(1);
        }
        if (!ok) break;
        Serial.println("#ACK");
        if (!sink(buf, got)) {
            reply("#FAIL write failed");
            ok = false;
            break;
        }
        received += got;
        const int percent = received * 100 / size;
        if (progress && percent != shown) {
            shown = percent;
            progress(percent);
        }
    }
    // What's still on its way (the rest of a chunk) isn't for the dev console
    if (!ok) discardInput();
    Serial.setRxBufferSize(kNormalRxBuffer);
    heap_caps_free(buf);
    return ok;
}

File target;

}  // namespace

size_t discardInput(uint32_t quietMs) {
    size_t dropped = 0;
    for (uint32_t quietSince = millis(); millis() - quietSince < quietMs;) {
        watchdog::feed();
        while (Serial.available()) {
            Serial.read();
            dropped++;
            quietSince = millis();
        }
        delay(1);
    }
    return dropped;
}

void receive(void (*progress)(int percent)) {
    Serial.println("#HEADER");
    const size_t size = Serial.parseInt();
    char md5[33] = {};
    if (Serial.read() != ',' || Serial.readBytes(md5, 32) != 32) return reply("#FAIL bad header");

    player::stop();  // nothing else wants the CPU or the flash meanwhile
    if (!Update.begin(size, U_FLASH)) {
        Serial.printf("#FAIL %s\n", Update.errorString());
        return;
    }
    Update.setMD5(md5);
    Serial.printf("usb_update: receiving %u bytes\n", size);
    const bool ok = transfer(size, [](const uint8_t *data, size_t n) { return Update.write((uint8_t *)data, n) == n; },
                             progress);
    if (!ok || !Update.end()) {
        if (ok) Serial.printf("#FAIL %s\n", Update.errorString());
        Update.abort();
        if (progress) progress(-1);  // the old firmware carries on
        return;
    }
    reply("#DONE");
    delay(300);  // for the line to get out
    ESP.restart();
}

void receiveFile() {
    Serial.println("#HEADER");
    const size_t size = Serial.parseInt();
    if (Serial.read() != ',') return reply("#FAIL bad header");
    const String path = Serial.readStringUntil('\n');
    if (!path.startsWith("/") || path.indexOf("..") >= 0) return reply("#FAIL bad path");
    if (!storage::checkForCard()) return reply("#FAIL no card");

    // Folders on the way, then a temporary file renamed at the end, so a cut
    // transfer never leaves half a file under the real name
    for (int slash = path.indexOf('/', 1); slash > 0; slash = path.indexOf('/', slash + 1)) {
        SD_MMC.mkdir(path.substring(0, slash));
    }
    const String tmp = path + ".part";
    target = SD_MMC.open(tmp, FILE_WRITE);
    if (!target) return reply("#FAIL can't create the file");
    Serial.printf("usb_update: receiving %u bytes for %s\n", size, path.c_str());
    const bool ok = transfer(size, [](const uint8_t *data, size_t n) { return target.write(data, n) == n; }, nullptr);
    target.close();
    if (!ok) {
        SD_MMC.remove(tmp);
        return;
    }
    SD_MMC.remove(path);
    if (!SD_MMC.rename(tmp, path)) return reply("#FAIL rename failed");
    reply("#DONE");
}

void deleteFile() {
    Serial.println("#HEADER");
    const String path = Serial.readStringUntil('\n');
    if (!path.startsWith("/") || path.indexOf("..") >= 0) return reply("#FAIL bad path");
    if (!storage::checkForCard()) return reply("#FAIL no card");
    reply(SD_MMC.remove(path) ? "#DONE" : "#FAIL not there");
}

}  // namespace usb_update
