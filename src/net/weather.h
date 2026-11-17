#pragma once

#include <stdint.h>
#include <time.h>

#include <vector>

// Weather from Open-Meteo (https://open-meteo.com, data CC BY 4.0), fetched by
// a background task so HTTP never blocks the UI. The UI polls revision().
namespace weather {

struct Current {
    float temp, feelsLike, wind;  // °C, °C, km/h
    uint8_t humidity, code;       // %, WMO weather code
    bool isDay;
};

struct Hour {
    time_t time;  // the first is the current hour
    float temp;
    uint8_t code, rainChance;  // WMO weather code, %
    bool isDay;
};

struct Day {
    time_t date;
    float min, max;
    uint8_t code, rainChance;
    time_t sunrise, sunset;
};

constexpr int kHours = 24;
constexpr int kDays = 6;

struct Forecast {
    time_t updated = 0;
    Current now{};
    Hour hours[kHours]{};
    int hourCount = 0;
    Day days[kDays]{};
    int dayCount = 0;
};

struct Place {
    char name[40];    // "Brno"
    char region[56];  // "South Moravian, Czechia", to tell same-named places apart
    float lat, lon;
    bool automatic;   // located by IP address
};

void begin();
void loop();  // from the UI task: saves a place located by IP

bool get(Forecast &out);  // false until the first forecast arrives
bool place(Place &out);   // false while the location is unknown
uint32_t revision();      // changes whenever forecast or place change

void setPlace(const Place &p);
void useAutomaticPlace();

// City search in the background: start it, then poll searchResults()
enum class SearchState { Idle, Busy, Done, Failed };
void search(const char *query);
SearchState searchResults(std::vector<Place> &out);

// WMO code to a Weather Icons glyph (see font_weather_*) and a description
const char *icon(uint8_t code, bool isDay);
const char *describe(uint8_t code);

}  // namespace weather
