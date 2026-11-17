#include "backup.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <SD_MMC.h>

#include <vector>

#include "net/weather.h"
#include "settings.h"
#include "storage.h"
#include "watchdog.h"

namespace backup {
namespace {

constexpr char kDir[] = "/backup";
constexpr char kWhen[] = "/backup/made";  // the time it was made, as text; written last

// The settings blob as settings.cpp keeps it: older versions are shorter
struct StoredSettings {
    uint32_t version;
    settings::Settings values;
};

// What goes in it: blobs kept in NVS, and files in internal flash. A blob on
// the card outside its sizes is no backup of ours, so nothing is restored.
struct Blob {
    const char *ns, *key, *file;
    size_t minSize, maxSize;
};
const Blob kBlobs[] = {
    {"radio", "settings", "/backup/settings.bin", sizeof(uint32_t), sizeof(StoredSettings)},
    {"weather", "place", "/backup/place.bin", sizeof(weather::Place), sizeof(weather::Place)},
};
const char *const kFiles[] = {"/data/alarms.json", "/data/stations.json"};

// Both ways, everything goes next to its place first (.tmp) and is checked;
// only then are the old ones replaced. So a failed save leaves the last
// backup whole, and a failed restore changes nothing.
String tmpOf(const String &path) { return path + ".tmp"; }

String onCard(const char *internalPath) { return String(kDir) + strrchr(internalPath, '/'); }

// From `fromPath` to the temporary next to `toPath`, all of it
bool copy(fs::FS &from, const String &fromPath, fs::FS &to, const String &toPath) {
    File in = from.open(fromPath, "r");
    if (!in) return false;
    const String tmp = tmpOf(toPath);
    File out = to.open(tmp, "w");
    if (!out) return false;
    uint8_t buf[512];
    size_t copied = 0;
    bool ok = true;
    while (ok && in.available()) {
        const size_t n = in.read(buf, sizeof(buf));
        ok = n > 0 && out.write(buf, n) == n;
        watchdog::feed();
        copied += n;
    }
    ok = ok && copied == in.size();
    out.close();
    if (!ok) to.remove(tmp);
    return ok;
}

bool writeTmp(fs::FS &to, const String &path, const std::vector<uint8_t> &data) {
    File f = to.open(tmpOf(path), FILE_WRITE);
    const bool ok = f && f.write(data.data(), data.size()) == data.size();
    if (f) f.close();
    if (!ok) to.remove(tmpOf(path));
    return ok;
}

// The temporary in its place, or `path` gone when there's none. LittleFS
// replaces in one step; FAT on the card needs the old one removed first.
bool commit(fs::FS &fs, const String &path, bool present) {
    if (!present) return !fs.exists(path) || fs.remove(path);
    return fs.rename(tmpOf(path), path) || (fs.remove(path) && fs.rename(tmpOf(path), path));
}

void dropTmps(fs::FS &fs, const std::vector<String> &paths) {
    for (const String &p : paths) {
        if (fs.exists(tmpOf(p))) fs.remove(tmpOf(p));
    }
}

// Empty when it isn't set
std::vector<uint8_t> readBlob(const Blob &b) {
    Preferences prefs;
    if (!prefs.begin(b.ns, true)) return {};  // nothing saved yet
    std::vector<uint8_t> data(prefs.isKey(b.key) ? prefs.getBytesLength(b.key) : 0);
    if (!data.empty() && prefs.getBytes(b.key, data.data(), data.size()) != data.size()) data.clear();
    prefs.end();
    return data;
}

// An empty one removes the key: it wasn't set when the backup was made
bool writeBlob(const Blob &b, const std::vector<uint8_t> &data) {
    Preferences prefs;
    if (!prefs.begin(b.ns, false)) return false;
    const bool ok = data.empty() ? !prefs.isKey(b.key) || prefs.remove(b.key)
                                 : prefs.putBytes(b.key, data.data(), data.size()) == data.size();
    prefs.end();
    return ok;
}

}  // namespace

bool save() {
    if (!storage::checkForCard()) return false;
    settings::saveNow();
    SD_MMC.mkdir(kDir);

    // Everything beside the last backup first
    std::vector<String> paths;     // on the card
    std::vector<bool> present;     // false: not set, so not in the backup either
    bool ok = true;
    for (const Blob &b : kBlobs) {
        const std::vector<uint8_t> data = readBlob(b);
        paths.push_back(b.file);
        present.push_back(!data.empty());
        ok = ok && (data.empty() || writeTmp(SD_MMC, b.file, data));
    }
    for (const char *path : kFiles) {
        paths.push_back(onCard(path));
        present.push_back(LittleFS.exists(path));
        ok = ok && (!present.back() || copy(LittleFS, path, SD_MMC, onCard(path)));
    }
    if (!ok) {
        dropTmps(SD_MMC, paths);
        return false;
    }

    // Then in place, unmarked meanwhile: a cut here leaves no backup that looks whole
    if (SD_MMC.exists(kWhen) && !SD_MMC.remove(kWhen)) ok = false;
    for (size_t i = 0; ok && i < paths.size(); i++) ok = commit(SD_MMC, paths[i], present[i]);
    if (ok) {
        File when = SD_MMC.open(kWhen, FILE_WRITE);
        ok = when && when.print((long long)time(nullptr)) > 0;
        if (when) when.close();
        if (!ok) SD_MMC.remove(kWhen);
    }
    dropTmps(SD_MMC, paths);  // what a failure left
    if (ok) Serial.println("backup: saved to the card");
    return ok;
}

time_t lastSaved() {
    if (!storage::hasCard()) return 0;
    File when = SD_MMC.open(kWhen, "r");
    return when ? (time_t)when.readString().toInt() : 0;
}

void restore() {
    if (!storage::checkForCard() || !SD_MMC.exists(kWhen)) return;

    // Read and copied beside the current ones first: the blobs into memory,
    // all checked before any is written, the files to temporaries in internal flash
    std::vector<std::vector<uint8_t>> blobs;
    for (const Blob &b : kBlobs) {
        File f = SD_MMC.open(b.file, "r");
        const size_t size = f ? f.size() : 0;  // no file: it wasn't set
        if (f && (size < b.minSize || size > b.maxSize)) {
            Serial.printf("backup: %s has the wrong size, nothing restored\n", b.file);
            return;
        }
        std::vector<uint8_t> data(size);
        if (f && f.read(data.data(), data.size()) != data.size()) return;
        blobs.push_back(data);
    }
    std::vector<String> paths;
    std::vector<bool> present;
    for (const char *path : kFiles) {
        paths.push_back(path);
        present.push_back(SD_MMC.exists(onCard(path)));
        if (present.back() && !copy(SD_MMC, onCard(path), LittleFS, path)) {
            dropTmps(LittleFS, paths);
            return;
        }
    }

    // All there: now replace
    bool ok = true;
    for (size_t i = 0; ok && i < blobs.size(); i++) ok = writeBlob(kBlobs[i], blobs[i]);
    for (size_t i = 0; ok && i < paths.size(); i++) ok = commit(LittleFS, paths[i], present[i]);
    dropTmps(LittleFS, paths);
    if (!ok) {
        Serial.println("backup: restoring failed part way");
        return;
    }
    Serial.println("backup: restored from the card, restarting");
    delay(200);
    ESP.restart();
}

}  // namespace backup
