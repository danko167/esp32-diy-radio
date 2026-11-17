#pragma once

#include <Arduino.h>
#include <time.h>

#include <vector>

#include "net/stations.h"

// Internet radio controls shared by the radio screens and the buttons
namespace radio {

void loop();  // call often: song history and the sleep timer

void play(const stations::Station &s);
void togglePlay();  // stop if playing, otherwise resume the last station
void step(int direction);  // next (+1) or previous (-1) favourite
bool active();  // playing or connecting a station

// Volume for radio and media, 0..100, saved in settings
void setVolume(int percent);
int volume();

// Songs from the stations' stream titles, since the radio was switched on
struct Song {
    String title;
    String station;
    time_t at;
};
std::vector<Song> songs();  // newest first
uint32_t songsRevision();

// Sleep timer: each call goes round off, 15, 30, 60, 90 minutes. When it runs
// out the radio fades out and stops.
void cycleSleep();
int sleepMinutesLeft();  // 0 when off

}  // namespace radio
