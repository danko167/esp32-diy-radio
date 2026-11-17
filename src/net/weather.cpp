#include "weather.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>

#include "app/psram_json.h"
#include "http.h"

namespace weather {
namespace {

constexpr uint32_t kRefreshMs = 15 * 60 * 1000;
constexpr uint32_t kRetryMs = 60 * 1000;
constexpr char kNamespace[] = "weather";

SemaphoreHandle_t mutex;
TaskHandle_t task;

// Shared between the weather task and the UI, guarded by `mutex`
Forecast forecast;
bool haveForecast = false;
Place where;
bool havePlace = false;
bool placeChanged = false;
bool placeLocated = false;  // by the task, for loop() to save: the task never writes flash
uint32_t rev = 0;
String searchQuery;
bool searchPending = false;
SearchState searchState = SearchState::Idle;
std::vector<Place> searchFound;

struct Lock {
    Lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(mutex); }
};

void savePlace(const Place *p) {
    Preferences prefs;
    prefs.begin(kNamespace, false);
    if (p) {
        prefs.putBytes("place", p, sizeof(Place));
    } else {
        prefs.remove("place");
    }
    prefs.end();
}

bool loadPlace(Place &out) {
    Preferences prefs;
    prefs.begin(kNamespace, false);
    const bool ok = prefs.isKey("place") && prefs.getBytesLength("place") == sizeof(Place) &&
                    prefs.getBytes("place", &out, sizeof(Place)) == sizeof(Place);
    prefs.end();
    // Text again, whatever the bytes were (a backup put back from a card)
    out.name[sizeof(out.name) - 1] = '\0';
    out.region[sizeof(out.region) - 1] = '\0';
    return ok;
}

bool locateByIp(Place &out) {
    JsonDocument doc(psram_json::allocator());
    if (!http::getJson("http://ip-api.com/json/?fields=status,city,country,lat,lon", doc)) return false;
    if (strcmp(doc["status"] | "", "success") != 0) return false;
    out = {};
    strlcpy(out.name, doc["city"] | "", sizeof(out.name));
    strlcpy(out.region, doc["country"] | "", sizeof(out.region));
    out.lat = doc["lat"] | 0.0f;
    out.lon = doc["lon"] | 0.0f;
    out.automatic = true;
    return true;
}

bool fetchForecast(const Place &p, Forecast &f) {
    char url[512];
    snprintf(url, sizeof(url),
             "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,"
             "weather_code,wind_speed_10m"
             "&hourly=temperature_2m,weather_code,is_day,precipitation_probability&forecast_hours=%d"
             "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
             "precipitation_probability_max,sunrise,sunset&forecast_days=%d"
             "&timezone=auto&timeformat=unixtime",
             p.lat, p.lon, kHours, kDays);
    // Only what's read below: not the units, the time zone and the rest
    JsonDocument filter(psram_json::allocator());
    filter["current"] = true;
    filter["hourly"] = true;
    filter["daily"] = true;
    JsonDocument doc(psram_json::allocator());
    if (!http::getJson(url, doc, &filter)) return false;

    JsonObject cur = doc["current"];
    if (cur.isNull()) return false;
    f = Forecast{};
    f.updated = time(nullptr);
    f.now.temp = cur["temperature_2m"] | 0.0f;
    f.now.feelsLike = cur["apparent_temperature"] | 0.0f;
    f.now.wind = cur["wind_speed_10m"] | 0.0f;
    f.now.humidity = cur["relative_humidity_2m"] | 0;
    f.now.code = cur["weather_code"] | 0;
    f.now.isDay = (cur["is_day"] | 1) == 1;

    JsonObject hourly = doc["hourly"];
    JsonArray hourTimes = hourly["time"];
    f.hourCount = min<int>(hourTimes.size(), kHours);
    for (int i = 0; i < f.hourCount; i++) {
        Hour &h = f.hours[i];
        h.time = hourTimes[i].as<uint32_t>();
        h.temp = hourly["temperature_2m"][i] | 0.0f;
        h.code = hourly["weather_code"][i] | 0;
        h.isDay = (hourly["is_day"][i] | 1) == 1;
        h.rainChance = hourly["precipitation_probability"][i] | 0;  // null when unknown
    }

    JsonObject daily = doc["daily"];
    JsonArray dayTimes = daily["time"];
    f.dayCount = min<int>(dayTimes.size(), kDays);
    for (int i = 0; i < f.dayCount; i++) {
        Day &d = f.days[i];
        d.date = dayTimes[i].as<uint32_t>();
        d.min = daily["temperature_2m_min"][i] | 0.0f;
        d.max = daily["temperature_2m_max"][i] | 0.0f;
        d.code = daily["weather_code"][i] | 0;
        d.rainChance = daily["precipitation_probability_max"][i] | 0;  // null when unknown
        d.sunrise = daily["sunrise"][i].as<uint32_t>();
        d.sunset = daily["sunset"][i].as<uint32_t>();
    }
    return true;
}

bool searchPlaces(const String &query, std::vector<Place> &out) {
    JsonDocument doc(psram_json::allocator());
    const String url =
        "http://geocoding-api.open-meteo.com/v1/search?count=8&language=en&format=json&name=" +
        http::urlEncode(query);
    if (!http::getJson(url, doc)) return false;
    out.clear();
    for (JsonObject r : doc["results"].as<JsonArray>()) {
        Place p{};
        strlcpy(p.name, r["name"] | "", sizeof(p.name));
        const char *admin = r["admin1"] | "";
        const char *country = r["country"] | "";
        snprintf(p.region, sizeof(p.region), "%s%s%s", admin, *admin && *country ? ", " : "",
                 country);
        p.lat = r["latitude"] | 0.0f;
        p.lon = r["longitude"] | 0.0f;
        out.push_back(p);
    }
    return true;
}

// Wakes every few seconds (or when notified) and does whatever is due
void run(void *) {
    uint32_t lastAttempt = 0;
    bool lastOk = false;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
        if (!WiFi.isConnected()) continue;

        String query;
        bool doSearch = false;
        {
            Lock lock;
            doSearch = searchPending;
            searchPending = false;
            query = searchQuery;
        }
        if (doSearch) {
            std::vector<Place> found;
            const bool ok = searchPlaces(query, found);
            Lock lock;
            searchFound = std::move(found);
            searchState = ok ? SearchState::Done : SearchState::Failed;
        }

        Place p;
        bool have, changed;
        {
            Lock lock;
            p = where;
            have = havePlace;
            changed = placeChanged;
            placeChanged = false;
        }
        const uint32_t interval = lastOk ? kRefreshMs : kRetryMs;
        if (!changed && lastAttempt != 0 && millis() - lastAttempt < interval) continue;
        lastAttempt = millis();

        if (!have) {
            have = locateByIp(p);
            if (have) {
                Serial.printf("weather: located %s, %s by IP\n", p.name, p.region);
                Lock lock;
                where = p;
                havePlace = true;
                placeLocated = true;
                rev++;
            }
        }
        Forecast f;
        lastOk = have && fetchForecast(p, f);
        if (lastOk) {
            Lock lock;
            forecast = f;
            haveForecast = true;
            rev++;
        }
    }
}

}  // namespace

void begin() {
    mutex = xSemaphoreCreateMutex();
    havePlace = loadPlace(where);
    // Core 0 is where Wi-Fi runs; the UI lives on core 1
    // Uses about 5.2 KB (the console's h shows the margin): 6 KB left under 1 KB spare.
    // The stack in PSRAM, so 8 KB more internal RAM: this task must never touch
    // internal flash (NVS, LittleFS), which turns PSRAM off meanwhile. It runs forever.
    xTaskCreatePinnedToCoreWithCaps(run, "weather", 8192, nullptr, 1, &task, 0, MALLOC_CAP_SPIRAM);
}

void loop() {
    Place p;
    {
        Lock lock;
        if (!placeLocated) return;
        placeLocated = false;
        p = where;
    }
    savePlace(&p);
}

bool get(Forecast &out) {
    Lock lock;
    if (haveForecast) out = forecast;
    return haveForecast;
}

bool place(Place &out) {
    Lock lock;
    if (havePlace) out = where;
    return havePlace;
}

uint32_t revision() {
    Lock lock;
    return rev;
}

void setPlace(const Place &p) {
    savePlace(&p);
    {
        Lock lock;
        where = p;
        where.automatic = false;
        havePlace = true;
        placeLocated = false;  // saved just now
        haveForecast = false;  // the old one is for somewhere else
        placeChanged = true;
        rev++;
    }
    xTaskNotifyGive(task);
}

void useAutomaticPlace() {
    savePlace(nullptr);
    {
        Lock lock;
        havePlace = false;
        haveForecast = false;
        placeChanged = true;
        placeLocated = false;  // not that one: located again
        rev++;
    }
    xTaskNotifyGive(task);
}

void search(const char *query) {
    {
        Lock lock;
        searchQuery = query;
        searchPending = true;
        searchState = SearchState::Busy;
        searchFound.clear();
    }
    xTaskNotifyGive(task);
}

SearchState searchResults(std::vector<Place> &out) {
    Lock lock;
    if (searchState == SearchState::Done) out = searchFound;
    return searchState;
}

}  // namespace weather
