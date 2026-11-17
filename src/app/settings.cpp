#include "settings.h"

#include <Arduino.h>
#include <Preferences.h>
#include <stddef.h>

namespace settings {
namespace {

constexpr char kNamespace[] = "radio";
constexpr char kKey[] = "settings";
// 2: added volume. 3: performance overlay off by default. 4: nightColour, in v3's padding.
// 5: homeTiles with the Recorder, at the end. 6: the Level in homeTiles (in
// v5's padding), screenLock and the Level's offsets after it
constexpr uint32_t kVersion = 6;
constexpr uint32_t kSaveDelayMs = 3000;

// v5 saves end with homeTiles' first 7 bytes and a padding byte: the Level's
// byte lands there, and what v6 added comes after
static_assert(offsetof(Settings, homeTiles) == 24 && offsetof(Settings, screenLock) == 32,
              "v5 saves load as load() expects");

struct Stored {
    uint32_t version;
    Settings values;
};

Settings current;
bool dirty = false;
uint32_t dirtySince = 0;

void save() {
    Preferences prefs;
    prefs.begin(kNamespace, false);
    Stored stored{kVersion, current};
    prefs.putBytes(kKey, &stored, sizeof(stored));
    prefs.end();
}

}  // namespace

Settings &get() { return current; }

NightColour nightColour() {
    return current.nightColour == NightColour::Green ? NightColour::Green : NightColour::Red;
}

void load() {
    Preferences prefs;
    prefs.begin(kNamespace, false);
    // A shorter blob is an older save: fields added since keep their defaults
    Stored stored{kVersion, Settings{}};
    const size_t length = prefs.isKey(kKey) ? prefs.getBytesLength(kKey) : 0;
    if (length >= sizeof(uint32_t) && length <= sizeof(stored) &&
        prefs.getBytes(kKey, &stored, length) == length) {
        if (stored.version == kVersion || stored.version == 4) {
            current = stored.values;
        } else if (stored.version == 5) {
            current = stored.values;
            // The Level, just before Settings in the order chosen (its byte was padding)
            uint8_t *tiles = current.homeTiles;
            int at = kHomeTiles - 1;
            for (int i = 0; i < kHomeTiles - 1; i++) {
                if (tiles[i] == 5) at = i;
            }
            for (int i = kHomeTiles - 1; i > at; i--) tiles[i] = tiles[i - 1];
            tiles[at] = 7;
        } else if (stored.version >= 1 && stored.version <= 3) {
            current = stored.values;
            // volume sits where v1 had a padding byte, so it can hold junk
            if (stored.version == 1) current.volume = Settings{}.volume;
            // It was on by default, redrawing 3 times a second forever; About still has it
            if (stored.version <= 2) current.perfOverlay = false;
            // Where v3 and older had padding
            current.nightColour = Settings{}.nightColour;
        }
        current.nightColour = nightColour();  // one it doesn't know is red
    }
    prefs.end();
}

void changed() {
    dirty = true;
    dirtySince = millis();
}

void loop() {
    if (dirty && millis() - dirtySince >= kSaveDelayMs) {
        dirty = false;
        save();
    }
}

void saveNow() {
    if (!dirty) return;
    dirty = false;
    save();
}

}  // namespace settings
