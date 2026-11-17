#include "storage.h"

#include <LittleFS.h>
#include <SD_MMC.h>

#include "hal/pins.h"

namespace storage {
namespace {

constexpr uint32_t kCardCheckMs = 3000;

volatile bool cardMounted = false;  // and not found gone since; other tasks read it
bool unmountDue = false;            // found gone, SD_MMC.end() waits for the uses
int uses = 0;                       // CardUses held by other tasks
portMUX_TYPE usesLock = portMUX_INITIALIZER_UNLOCKED;  // for uses and cardMounted together
uint32_t cardRev = 0;
uint32_t lastCardCheck = 0;
alignas(4) uint8_t sector[512];

int usesNow() {
    taskENTER_CRITICAL(&usesLock);
    const int n = uses;
    taskEXIT_CRITICAL(&usesLock);
    return n;
}

// A card found gone: no new uses from now on, unmounted when the last one goes
void cardGone() {
    taskENTER_CRITICAL(&usesLock);
    cardMounted = false;
    taskEXIT_CRITICAL(&usesLock);
    unmountDue = true;
    cardRev++;
}

// False while a use still holds the card that went
bool finishUnmount() {
    if (!unmountDue) return true;
    if (usesNow() > 0) return false;
    SD_MMC.end();
    unmountDue = false;
    return true;
}

bool mountCard() {
    const uint32_t start = millis();
    cardMounted = SD_MMC.begin("/sdcard", true /* 1-bit bus */);
    if (cardMounted) {
        Serial.printf("storage: microSD card, %llu MB, %llu MB free (mounted in %lu ms)\n",
                      SD_MMC.cardSize() / (1024 * 1024), (SD_MMC.totalBytes() - SD_MMC.usedBytes()) / (1024 * 1024),
                      millis() - start);
        SD_MMC.mkdir("/sounds");
    }
    return cardMounted;
}

}  // namespace

bool begin() {
    // Formats the partition on first use; "spiffs" is its label in partitions.csv
    if (!LittleFS.begin(true, "/littlefs", 10, "spiffs")) {
        Serial.println("storage: internal file system failed");
        return false;
    }
    LittleFS.mkdir("/data");
    LittleFS.mkdir("/sounds");

    SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_D0);
    mountCard();
    lastCardCheck = millis();
    return true;
}

void loop() {
    if (!finishUnmount() || !cardMounted || millis() - lastCardCheck < kCardCheckMs) return;
    // Not while another task uses it: checked again as soon as it lets go
    if (usesNow() > 0) return;
    lastCardCheck = millis();
    // Straight from the card, past the file system's cache: is it still there?
    if (SD_MMC.readRAW(sector, 0)) return;
    Serial.println("storage: microSD card taken out");
    cardGone();
    finishUnmount();
}

bool checkForCard() {
    if (cardMounted) return true;
    if (!finishUnmount()) return false;
    if (mountCard()) {
        lastCardCheck = millis();
        cardRev++;
    }
    return cardMounted;
}

fs::FS &internal() { return LittleFS; }

bool hasCard() { return cardMounted; }

fs::FS &card() { return SD_MMC; }

uint32_t cardRevision() { return cardRev; }

bool CardUse::take() {
    if (held) return true;
    taskENTER_CRITICAL(&usesLock);
    held = cardMounted;
    if (held) uses++;
    taskEXIT_CRITICAL(&usesLock);
    return held;
}

void CardUse::release() {
    if (!held) return;
    held = false;
    taskENTER_CRITICAL(&usesLock);
    uses--;
    taskEXIT_CRITICAL(&usesLock);
}

bool readFile(const char *path, String &out) {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    out = f.readString();
    return true;
}

bool writeFile(const char *path, const String &text) {
    // Write next to it first, so a power cut never leaves half a file
    const String tmp = String(path) + ".tmp";
    File f = LittleFS.open(tmp, "w");
    if (!f) return false;
    const bool ok = f.print(text) == text.length();
    f.close();
    return ok && LittleFS.rename(tmp, path);
}

}  // namespace storage
