#include "radio.h"

#include "alarms.h"
#include "audio/player.h"
#include "settings.h"

namespace radio {
namespace {

constexpr size_t kMaxSongs = 30;
constexpr uint32_t kSleepFadeMs = 20 * 1000;
const int kSleepSteps[] = {0, 15, 30, 60, 90};
constexpr int kSleepStepCount = sizeof(kSleepSteps) / sizeof(kSleepSteps[0]);

std::vector<Song> heard;
uint32_t heardRev = 0;
uint32_t seenPlayer = UINT32_MAX;
String lastTitle, lastSource;

int sleepStep = 0;
uint32_t sleepAt = 0;  // millis() when the radio goes off, 0 = no timer

void cancelSleep() {
    if (!sleepAt) return;
    sleepAt = 0;
    sleepStep = 0;
    player::setVolume(volume());  // undo a fade in progress
}

void recordSong() {
    const player::Status st = player::status();
    if (strncmp(st.source, "http", 4) != 0 || st.state != player::State::Playing) return;
    if (lastSource != st.source) {  // new station: the same song may come round again
        lastSource = st.source;
        lastTitle = "";
    }
    if (!st.title[0] || lastTitle == st.title) return;
    lastTitle = st.title;
    heard.insert(heard.begin(), {st.title, st.name, time(nullptr)});
    if (heard.size() > kMaxSongs) heard.resize(kMaxSongs);
    heardRev++;
}

void runSleepTimer() {
    if (!sleepAt) return;
    if (alarms::ringing()) {  // an alarm took over: the timer goes, the volume is the alarm's
        sleepAt = 0;
        sleepStep = 0;
        return;
    }
    if (!active()) {  // stopped by hand
        cancelSleep();
        return;
    }
    const int32_t left = sleepAt - millis();
    if (left <= 0) {
        player::stop();
        cancelSleep();
    } else if (left < (int32_t)kSleepFadeMs) {
        const uint8_t v = volume() * left / kSleepFadeMs;
        if (v != player::volume()) player::setVolume(v);
    }
}

}  // namespace

void loop() {
    if (player::revision() != seenPlayer) {
        seenPlayer = player::revision();
        recordSong();
    }
    runSleepTimer();
}

void play(const stations::Station &s) {
    player::playUrl(s.url.c_str(), s.name.c_str());
    stations::setLast(s);
}

bool active() {
    const player::Status st = player::status();
    return strncmp(st.source, "http", 4) == 0 &&
           (st.state == player::State::Playing || st.state == player::State::Connecting);
}

void togglePlay() {
    if (active()) {
        player::stop();
        return;
    }
    stations::Station s;
    if (stations::last(s)) play(s);
}

void step(int direction) {
    const auto favs = stations::favourites();
    if (favs.empty()) return;
    stations::Station last;
    int index = -1;
    if (stations::last(last)) {
        for (size_t i = 0; i < favs.size(); i++) {
            if (favs[i].url == last.url) index = i;
        }
    }
    const int n = favs.size();
    index = index < 0 ? (direction > 0 ? 0 : n - 1) : ((index + direction) % n + n) % n;
    play(favs[index]);
}

void setVolume(int percent) {
    const uint8_t v = constrain(percent, 0, 100);
    settings::get().volume = v;
    settings::changed();
    player::setVolume(v);
}

int volume() { return settings::get().volume; }

std::vector<Song> songs() { return heard; }

uint32_t songsRevision() { return heardRev; }

void cycleSleep() {
    if (!active()) return;  // nothing to switch off
    const int step = (sleepStep + 1) % kSleepStepCount;
    cancelSleep();
    sleepStep = step;
    if (kSleepSteps[step]) sleepAt = millis() + kSleepSteps[step] * 60 * 1000;
}

int sleepMinutesLeft() {
    if (!sleepAt) return 0;
    const int32_t left = sleepAt - millis();
    return left <= 0 ? 0 : (left + 59999) / 60000;
}

}  // namespace radio
