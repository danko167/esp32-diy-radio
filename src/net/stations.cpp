#include "stations.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>

#include <algorithm>

#include "app/psram_json.h"
#include "app/storage.h"
#include "http.h"

namespace stations {
namespace {

constexpr char kFile[] = "/data/stations.json";
constexpr int kMaxFavourites = 40;
constexpr int kResultLimit = 40;
constexpr size_t kMaxRecent = 10;
// radio-browser.info is run by volunteers on several mirrors
const char *const kServers[] = {"de1.api.radio-browser.info", "nl1.api.radio-browser.info",
                                "at1.api.radio-browser.info"};

// Favourites and last are only touched from the UI thread
std::vector<Station> favs;
std::vector<Station> recentlyPlayed;
Station lastPlayed;
bool haveLast = false;

// Shared with the lookup task, guarded by `mutex`
SemaphoreHandle_t mutex;
TaskHandle_t task;
enum class Job { None, Popular, Search };
Job pending = Job::None;
String query;
State state = State::Idle;
std::vector<Station> found;
String country, countryCode;
uint32_t rev = 0;

struct Lock {
    Lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(mutex); }
};

void toJson(const Station &s, JsonObject o) {
    o["name"] = s.name;
    o["url"] = s.url;
    o["country"] = s.country;
    o["codec"] = s.codec;
    o["bitrate"] = s.bitrate;
}

Station fromJson(JsonObjectConst o) {
    Station s;
    s.name = o["name"] | "";
    s.url = o["url"] | "";
    s.country = o["country"] | "";
    s.codec = o["codec"] | "";
    s.bitrate = o["bitrate"] | 0;
    return s;
}

void save() {
    JsonDocument doc(psram_json::allocator());
    JsonArray arr = doc["favourites"].to<JsonArray>();
    for (const Station &s : favs) toJson(s, arr.add<JsonObject>());
    if (haveLast) toJson(lastPlayed, doc["last"].to<JsonObject>());
    JsonArray recentArr = doc["recent"].to<JsonArray>();
    for (const Station &s : recentlyPlayed) toJson(s, recentArr.add<JsonObject>());
    String text;
    psram_json::serialize(doc, text);
    storage::writeFile(kFile, text);
}

void load() {
    String text;
    if (!storage::readFile(kFile, text)) return;
    JsonDocument doc(psram_json::allocator());
    if (!psram_json::parse(doc, text)) return;
    for (JsonObjectConst o : doc["favourites"].as<JsonArrayConst>()) favs.push_back(fromJson(o));
    for (JsonObjectConst o : doc["recent"].as<JsonArrayConst>()) recentlyPlayed.push_back(fromJson(o));
    if (doc["last"].is<JsonObjectConst>()) {
        lastPlayed = fromJson(doc["last"]);
        haveLast = !lastPlayed.url.isEmpty();
    }
}

bool locate() {
    JsonDocument doc(psram_json::allocator());
    if (!http::getJson("http://ip-api.com/json/?fields=status,country,countryCode", doc)) return false;
    if (strcmp(doc["status"] | "", "success") != 0) return false;
    Lock lock;
    country = doc["country"] | "";
    countryCode = doc["countryCode"] | "";
    return !countryCode.isEmpty();
}

bool fetch(const String &path, std::vector<Station> &out) {
    JsonDocument filter(psram_json::allocator());
    filter[0]["name"] = true;
    filter[0]["url_resolved"] = true;
    filter[0]["countrycode"] = true;
    filter[0]["codec"] = true;
    filter[0]["bitrate"] = true;
    for (const char *server : kServers) {
        JsonDocument doc(psram_json::allocator());
        if (!http::getJson(String("http://") + server + path, doc, &filter)) continue;
        out.clear();
        for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
            Station s;
            s.name = o["name"] | "";
            s.name.trim();
            s.url = o["url_resolved"] | "";
            s.country = o["countrycode"] | "";
            s.codec = o["codec"] | "";
            s.bitrate = o["bitrate"] | 0;
            if (!s.name.isEmpty() && !s.url.isEmpty()) out.push_back(s);
        }
        return true;
    }
    return false;
}

void run(void *) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        Job job;
        String text;
        {
            Lock lock;
            job = pending;
            pending = Job::None;
            text = query;
        }
        if (job == Job::None) continue;

        const String params = String("hidebroken=true&order=clickcount&reverse=true&limit=") +
                              kResultLimit;
        std::vector<Station> list;
        bool ok = WiFi.isConnected();
        if (ok && job == Job::Popular) {
            ok = (!countryCode.isEmpty() || locate()) &&
                 fetch("/json/stations/bycountrycodeexact/" + countryCode + "?" + params, list);
        } else if (ok) {
            ok = fetch("/json/stations/search?name=" + http::urlEncode(text) + "&" + params, list);
        }

        Lock lock;
        found = std::move(list);
        state = ok ? State::Done : State::Failed;
        rev++;
    }
}

void start(Job job, const String &text) {
    {
        Lock lock;
        pending = job;
        query = text;
        state = State::Busy;
        rev++;
    }
    xTaskNotifyGive(task);
}

}  // namespace

void begin() {
    mutex = xSemaphoreCreateMutex();
    load();
    // Uses about 3.5 KB with search and the popular list (the console's h shows the margin).
    // The stack in PSRAM, so 6 KB more internal RAM: this task must never touch
    // internal flash (save() is for the UI task only), which turns PSRAM off
    // meanwhile. It runs forever.
    xTaskCreatePinnedToCoreWithCaps(run, "stations", 6144, nullptr, 1, &task, 0, MALLOC_CAP_SPIRAM);
}

std::vector<Station> favourites() { return favs; }

bool isFavourite(const String &url) {
    for (const Station &s : favs) {
        if (s.url == url) return true;
    }
    return false;
}

void toggleFavourite(const Station &s) {
    for (auto it = favs.begin(); it != favs.end(); ++it) {
        if (it->url == s.url) {
            favs.erase(it);
            save();
            Lock lock;
            rev++;
            return;
        }
    }
    if (favs.size() >= kMaxFavourites) return;
    favs.push_back(s);
    save();
    Lock lock;
    rev++;
}

void setFavourites(const std::vector<Station> &list) {
    std::vector<Station> next;
    for (const Station &s : list) {
        if (next.size() >= kMaxFavourites) break;
        // Keep what's known about stations already there (country, codec...)
        Station kept = s;
        for (const Station &old : favs) {
            if (old.url == s.url) {
                kept = old;
                kept.name = s.name;
            }
        }
        next.push_back(kept);
    }
    favs = next;
    save();
    Lock lock;
    rev++;
}

bool last(Station &out) {
    if (haveLast) out = lastPlayed;
    return haveLast;
}

void setLast(const Station &s) {
    if (haveLast && lastPlayed.url == s.url) return;
    lastPlayed = s;
    haveLast = true;
    recentlyPlayed.erase(std::remove_if(recentlyPlayed.begin(), recentlyPlayed.end(),
                                        [&](const Station &r) { return r.url == s.url; }),
                         recentlyPlayed.end());
    recentlyPlayed.insert(recentlyPlayed.begin(), s);
    if (recentlyPlayed.size() > kMaxRecent) recentlyPlayed.resize(kMaxRecent);
    save();
}

std::vector<Station> recent() { return recentlyPlayed; }

void loadPopular() { start(Job::Popular, ""); }

void search(const String &text) { start(Job::Search, text); }

State results(std::vector<Station> &out) {
    Lock lock;
    if (state == State::Done) out = found;
    return state;
}

String countryName() {
    Lock lock;
    return country;
}

uint32_t revision() {
    Lock lock;
    return rev;
}

}  // namespace stations
