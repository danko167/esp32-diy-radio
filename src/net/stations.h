#pragma once

#include <Arduino.h>

#include <vector>

// Internet radio stations: favourites saved on the device, and lookups in the
// free community directory at radio-browser.info, done in the background.
namespace stations {

struct Station {
    String name;
    String url;
    String country;        // ISO code, "HR"
    String codec;          // "MP3", "AAC"
    uint16_t bitrate = 0;  // kbps, 0 when unknown
};

void begin();  // storage must be mounted

std::vector<Station> favourites();
bool isFavourite(const String &url);
void toggleFavourite(const Station &s);
void setFavourites(const std::vector<Station> &list);  // all at once, in this order (the upload page)

bool last(Station &out);  // last played, to resume with one tap
void setLast(const Station &s);
std::vector<Station> recent();  // played lately, newest first

// Directory lookups: start one, then poll results()
enum class State { Idle, Busy, Done, Failed };
void loadPopular();  // most listened in the listener's country (found by IP)
void search(const String &text);
State results(std::vector<Station> &out);
String countryName();  // "Croatia", empty until known

uint32_t revision();  // changes when favourites or results change

}  // namespace stations
