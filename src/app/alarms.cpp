#include "alarms.h"

#include <ArduinoJson.h>
#include <esp_attr.h>
#include <esp_system.h>

#include <algorithm>
#include <cstddef>

#include "audio/player.h"
#include "audio/recorder.h"
#include "net/upload_server.h"
#include "audio/tones.h"
#include "hal/rtc.h"
#include "psram_json.h"
#include "settings.h"
#include "storage.h"

namespace alarms {
namespace {

constexpr char kFile[] = "/data/alarms.json";
constexpr int kMaxAlarms = 20;
constexpr uint32_t kFadeInMs = 60 * 1000;
constexpr uint32_t kGiveUpMs = 15 * 60 * 1000;  // stop ringing nobody answers
constexpr uint8_t kFadeStartVolume = 5;
constexpr time_t kCatchUpS = 10 * 60;  // missed by up to this (clock jump, blocking update): still rings
constexpr time_t kBootLookBackS = 2 * 60;
// Whether it's heard: sound must go out within kStartMs of asking for it and
// never stop for kStallMs. The first kLeftoverMs may still be the sound from
// before (the radio) going out, so they don't count.
constexpr uint32_t kStartMs = 5000;
constexpr uint32_t kStallMs = 3000;
constexpr uint32_t kLeftoverMs = 1000;
constexpr uint32_t kFailedGraceMs = 3000;  // a Failed state left over from before isn't ours

// What plays: the alarm's own sound, then the chime, the chime once more,
// then a restart (once) and the chime after it
enum Attempt : uint8_t { kOwnSound, kChime, kChimeAgain, kAfterRestart };

std::vector<Alarm> all;
uint32_t nextId = 1;
uint32_t rev = 0;

uint32_t ringingId = 0;
uint32_t ringStarted = 0;  // millis()
time_t ringStartedAt = 0;  // the same, as a time, which a restart doesn't reset
uint32_t snoozeId = 0;
time_t snoozeAt = 0;
uint8_t attempt = kOwnSound;
time_t lastChecked = 0;  // alarms due after this have yet to ring

// The sound being listened for
uint32_t soundStarted = 0;
uint32_t framesSeen = 0;
uint32_t heardAt = 0;
bool heard = false;

// Ringing and snooze, kept where a restart (a crash, the watchdog, our own)
// doesn't wipe them, but power loss does. Never initialised: the magic number
// and checksum tell what's ours from what was there.
struct Kept {
    int64_t ringStartedAt;  // the 64-bit ones first: no padding inside the checksum
    int64_t snoozeAt;
    uint32_t magic;
    uint32_t ringingId;
    uint32_t snoozeId;
    uint32_t attempt;
    uint32_t sum;
};
constexpr uint32_t kKeptMagic = 0xA1A2B5E1;
RTC_NOINIT_ATTR Kept kept;

// A ring a restart cut short, picked up once the time is known
uint32_t resumeId = 0;
time_t resumeStartedAt = 0;
uint8_t resumeAttempt = kOwnSound;

// FNV-1a over everything before `sum`, a word at a time (RTC memory is best
// read in whole words)
uint32_t checksum(const Kept &k) {
    const uint32_t *p = reinterpret_cast<const uint32_t *>(&k);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < offsetof(Kept, sum) / 4; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

// After every change to the ringing or snooze state
void remember() {
    kept.ringStartedAt = ringingId ? ringStartedAt : 0;
    kept.snoozeAt = snoozeId ? snoozeAt : 0;
    kept.magic = kKeptMagic;
    kept.ringingId = ringingId;
    kept.snoozeId = snoozeId;
    kept.attempt = ringingId ? attempt : kOwnSound;
    kept.sum = checksum(kept);
}

Alarm *find(uint32_t id) {
    for (Alarm &a : all) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

void save() {
    JsonDocument doc(psram_json::allocator());
    JsonArray arr = doc.to<JsonArray>();
    for (const Alarm &a : all) {
        JsonObject o = arr.add<JsonObject>();
        o["id"] = a.id;
        o["on"] = a.enabled;
        o["h"] = a.hour;
        o["m"] = a.minute;
        o["days"] = a.days;
        o["label"] = a.label;
        o["sound"] = a.sound;
        o["soundName"] = a.soundName;
        o["volume"] = a.volume;
        o["gentle"] = a.gentle;
        o["snooze"] = a.snoozeMinutes;
    }
    String text;
    psram_json::serialize(doc, text);
    storage::writeFile(kFile, text);
}

void load() {
    String text;
    if (!storage::readFile(kFile, text)) return;
    JsonDocument doc(psram_json::allocator());
    if (!psram_json::parse(doc, text)) return;
    // Numbers kept to what the alarm screen can set: a volume under the fade's
    // start would make the fade wrap round, a snooze of 0 ring again at once
    for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
        Alarm a;
        a.id = o["id"] | 0;
        a.enabled = o["on"] | true;
        a.hour = std::clamp(o["h"] | 7, 0, 23);
        a.minute = std::clamp(o["m"] | 0, 0, 59);
        a.days = (o["days"] | 0) & kEveryDay;
        a.label = o["label"] | "";
        a.sound = o["sound"] | "tone:chime";
        a.soundName = o["soundName"] | "Chime";
        a.volume = std::clamp(o["volume"] | 60, 10, 100);
        a.gentle = o["gentle"] | true;
        a.snoozeMinutes = std::clamp(o["snooze"] | 9, 5, 20);
        if (a.id == 0) continue;
        nextId = max(nextId, a.id + 1);
        all.push_back(a);
    }
}

uint8_t dayBit(const struct tm &t) { return 1 << ((t.tm_wday + 6) % 7); }  // Monday = bit 0

// From now on, sound has to come out
void listen() {
    soundStarted = millis();
    framesSeen = player::framesPlayed();
    heard = false;
}

void startSound(const Alarm &a) {
    // Past the first attempt (a restart cut the ring short, perhaps brought
    // down by the alarm's own sound) it's the chime, from internal flash
    if (attempt != kOwnSound || !tones::play(a.sound)) {
        attempt = max<uint8_t>(attempt, kChime);
        tones::play("tone:chime");
    }
    player::setVolume(a.gentle ? kFadeStartVolume : a.volume);
    listen();
}

// `startedAt` is when it first rang: earlier when resuming after a restart,
// so the fade and the 15 minutes go on from there
void ring(Alarm &a, time_t startedAt, uint8_t fromAttempt) {
    Serial.printf("alarms: ringing %02u:%02u %s\n", a.hour, a.minute, a.label.c_str());
    // Uploads have the radio to themselves, but never at the cost of an alarm:
    // a page left open would otherwise keep the radio silent
    upload_server::stop("Stopped for an alarm");
    recorder::stop("Stopped for an alarm");
    const time_t elapsed = std::clamp<time_t>(time(nullptr) - startedAt, 0, kGiveUpMs / 1000);
    ringingId = a.id;
    ringStartedAt = startedAt;
    ringStarted = millis() - uint32_t(elapsed) * 1000;
    snoozeId = 0;
    snoozeAt = 0;
    attempt = fromAttempt;
    startSound(a);
    remember();
    rev++;
}

void stopRinging() {
    ringingId = 0;
    attempt = kOwnSound;
    player::stop();
    player::setVolume(settings::get().volume);  // back to the radio level
    remember();
    rev++;
}

// The sound isn't coming out: the next attempt
void fallBack(const char *why) {
    Serial.printf("alarms: %s, ", why);
    if (attempt == kChimeAgain) {
        // Not even the chime, twice: the player is stuck. A restart starts it
        // afresh, and the ring goes on after it (see begin()); only once, after
        // that it's the chime again and again
        Serial.println("restarting");
        attempt = kAfterRestart;
        remember();
        esp_restart();
    }
    Serial.println("the chime instead");
    if (attempt < kChimeAgain) attempt++;
    remember();
    tones::play("tone:chime");
    listen();
}

// Judged by what goes out to the speaker, not the player's state: a broken
// file, a card gone mid-ring, a dropped request or a stuck decoder all end
// up as no frames. The volume (a fade starts low) doesn't stop the frames.
void checkHeard() {
    const uint32_t since = millis() - soundStarted;
    const uint32_t frames = player::framesPlayed();
    if (frames != framesSeen) {
        framesSeen = frames;
        if (since > kLeftoverMs) {
            heard = true;
            heardAt = millis();
        }
        return;
    }
    if (since > kFailedGraceMs && player::status().state == player::State::Failed) {
        fallBack("the sound failed");
    } else if (heard ? millis() - heardAt > kStallMs : since > kStartMs) {
        fallBack(heard ? "the sound stopped" : "no sound");
    }
}

// A one-off is done once it has rung
void retireOneOff(uint32_t id) {
    Alarm *a = find(id);
    if (!a || a->days != 0 || !a->enabled) return;
    a->enabled = false;
    save();
}

// Rings whatever came due since the last look, once a second. Also while
// another alarm rings: the later one takes over, so a backup alarm still
// goes off when the first is slept through. nextRing() works in local time,
// so a daylight saving change neither skips an alarm nor rings it twice.
void ringDue(time_t now) {
    if (now == lastChecked) return;
    // At boot it looks back a little: an alarm due while the radio restarted
    // (the watchdog, a crash, an update) still rings. One that rang just before
    // a crash may ring again; better twice than not at all.
    if (lastChecked == 0) {
        lastChecked = now - kBootLookBackS;
    } else if (now < lastChecked - kCatchUpS) {
        lastChecked = now - 1;  // a big step back
    }
    if (now < lastChecked) return;  // a small step back: wait for it, so nothing rings twice
    const time_t since = max(lastChecked, now - kCatchUpS);
    lastChecked = now;

    Alarm *due = nullptr;
    time_t dueAt = 0;
    for (Alarm &a : all) {
        // A snoozed one rings when its snooze is up (the look-back after a
        // restart would otherwise ring it again at once)
        if (a.id == ringingId || a.id == snoozeId) continue;
        const time_t when = nextRing(a, since);
        if (when && when <= now && when >= dueAt) {
            due = &a;
            dueAt = when;
        }
    }
    if (!due) return;
    if (ringingId) retireOneOff(ringingId);
    ring(*due, now, kOwnSound);
}

// What a restart left: a snooze goes on, a ring is picked up in loop(). Not
// after power-on, where this memory holds nothing of ours.
void restore() {
    const bool ours = esp_reset_reason() != ESP_RST_POWERON && kept.magic == kKeptMagic &&
                      kept.sum == checksum(kept);
    if (ours && kept.snoozeId && find(kept.snoozeId)) {
        snoozeId = kept.snoozeId;
        snoozeAt = kept.snoozeAt;
        Serial.printf("alarms: still snoozed after the restart\n");
    }
    if (ours && kept.ringingId) {
        resumeId = kept.ringingId;
        resumeStartedAt = kept.ringStartedAt;
        // Died with its own sound playing: that may be why, so the chime
        resumeAttempt = std::clamp<uint32_t>(kept.attempt, kChime, kAfterRestart);
    }
    // Ours stays as it is till resume(): another restart before then keeps the ring
    if (!ours) remember();
}

void resume(time_t now) {
    Alarm *a = find(resumeId);
    resumeId = 0;
    if (!a || now - resumeStartedAt >= time_t(kGiveUpMs / 1000)) {  // gone, or its time ran out meanwhile
        if (a) retireOneOff(a->id);
        remember();
        return;
    }
    Serial.printf("alarms: ringing on after the restart\n");
    ring(*a, std::min(resumeStartedAt, now), resumeAttempt);
}

}  // namespace

void begin() {
    load();
    restore();
}

void loop() {
    if (!rtc::timeValid()) return;
    const time_t now = time(nullptr);

    if (resumeId) resume(now);

    if (ringingId) {
        const Alarm *a = find(ringingId);
        if (!a || millis() - ringStarted > kGiveUpMs) {
            dismiss();
            return;
        }
        checkHeard();
        // Set each time, so nothing else (the radio's sleep timer once) changes it
        uint8_t v = a->volume;
        if (a->gentle) {
            const uint32_t elapsed = millis() - ringStarted;
            v = kFadeStartVolume + (a->volume - kFadeStartVolume) * min(elapsed, kFadeInMs) / kFadeInMs;
        }
        if (v != player::volume()) player::setVolume(v);
    } else if (snoozeId && now >= snoozeAt) {
        Alarm *a = find(snoozeId);
        snoozeId = 0;
        remember();
        if (a) ring(*a, now, kOwnSound);
        return;
    }

    ringDue(now);
}

std::vector<Alarm> list() {
    std::vector<Alarm> out = all;
    std::sort(out.begin(), out.end(), [](const Alarm &x, const Alarm &y) {
        return x.hour * 60 + x.minute < y.hour * 60 + y.minute;
    });
    return out;
}

uint32_t revision() { return rev; }

void save(Alarm a) {
    a.enabled = true;  // saving an alarm means you want it
    if (a.id == 0) {
        if (all.size() >= kMaxAlarms) return;
        a.id = nextId++;
        all.push_back(a);
    } else if (Alarm *existing = find(a.id)) {
        *existing = a;
    }
    save();
    rev++;
}

void remove(uint32_t id) {
    all.erase(std::remove_if(all.begin(), all.end(), [&](const Alarm &a) { return a.id == id; }),
              all.end());
    if (snoozeId == id) snoozeId = 0;
    remember();
    save();
    rev++;
}

void setEnabled(uint32_t id, bool on) {
    Alarm *a = find(id);
    if (!a || a->enabled == on) return;
    a->enabled = on;
    if (!on && snoozeId == id) snoozeId = 0;
    remember();
    save();
    rev++;
}

time_t nextRing(const Alarm &a, time_t now) {
    if (!a.enabled) return 0;
    struct tm t;
    localtime_r(&now, &t);
    for (int day = 0; day < 8; day++) {
        struct tm c = t;
        c.tm_mday += day;
        c.tm_hour = a.hour;
        c.tm_min = a.minute;
        c.tm_sec = 0;
        c.tm_isdst = -1;
        const time_t when = mktime(&c);  // also normalises the date and weekday
        if (when <= now) continue;
        if (a.days && !(a.days & dayBit(c))) continue;
        return when;
    }
    return 0;
}

time_t nextRing(time_t now) {
    time_t best = snoozeId ? snoozeAt : 0;
    for (const Alarm &a : all) {
        const time_t when = nextRing(a, now);
        if (when && (!best || when < best)) best = when;
    }
    return best;
}

String describeDays(uint8_t days) {
    if (days == 0) return "Once";
    if (days == kEveryDay) return "Every day";
    if (days == kWeekdays) return "Weekdays";
    if (days == kWeekend) return "Weekends";
    static const char *const kNames[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    String out;
    for (int i = 0; i < 7; i++) {
        if (!(days & (1 << i))) continue;
        if (!out.isEmpty()) out += ", ";
        out += kNames[i];
    }
    return out;
}

bool ringing(Alarm *which) {
    if (!ringingId) return false;
    if (which) {
        if (const Alarm *a = find(ringingId)) *which = *a;
    }
    return true;
}

void snooze() {
    const Alarm *a = find(ringingId);
    if (!a) return;
    snoozeId = a->id;
    snoozeAt = time(nullptr) + a->snoozeMinutes * 60;
    stopRinging();
}

void dismiss() {
    retireOneOff(ringingId);
    snoozeId = 0;
    stopRinging();
}

time_t snoozedUntil() { return snoozeId ? snoozeAt : 0; }

// Like stopping it: a one-off is done
void cancelSnooze() {
    if (!snoozeId) return;
    retireOneOff(snoozeId);
    snoozeId = 0;
    remember();
    rev++;
}

}  // namespace alarms
