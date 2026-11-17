#pragma once

#include <FS.h>

#include <stdint.h>

// Internal flash file system (LittleFS on the "spiffs" partition) and the
// optional microSD card.
namespace storage {

bool begin();  // mounts both; the SD card may simply be missing
// The board has no card-detect pin. While a card is mounted, this checks
// every few seconds that it's still there (a quick read).
void loop();

fs::FS &internal();
bool hasCard();
// Mounts a card put in since; for whatever is about to use one. Looking for a
// missing card stalls ~50 ms (and the SD driver logs errors), so it isn't polled.
bool checkForCard();
fs::FS &card();          // only valid when hasCard()
uint32_t cardRevision();  // changes when a card comes or goes

// The card belongs to the UI task: it mounts it, checks it's still there and
// unmounts one taken out. Other tasks (the upload server, the player) hold a
// CardUse from before opening a card file until after closing it: meanwhile
// the check waits (their own I/O errors show a card pulled out), and a card
// found gone is unmounted only once the last use lets go, never under an
// open file. The UI task needs none, as long as it closes its files before
// returning to the loop.
class CardUse {
  public:
    explicit CardUse(bool now = true) {
        if (now) take();
    }
    ~CardUse() { release(); }
    CardUse(const CardUse &) = delete;
    CardUse &operator=(const CardUse &) = delete;
    bool take();  // false, holding nothing, without a card or once it's been found gone
    void release();
    explicit operator bool() const { return held; }

  private:
    bool held = false;
};

// User data lives in internal flash under /data
bool readFile(const char *path, String &out);
bool writeFile(const char *path, const String &text);

}  // namespace storage
