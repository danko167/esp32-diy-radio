// The upload server's favourite stations: the page sends the whole list

#include <ArduinoJson.h>

#include <vector>

#include "app/psram_json.h"
#include "net/stations.h"
#include "upload_internal.h"

namespace upload_server {
namespace {

// What the player keeps of them (player.h: char[64] and char[200], with their ends)
constexpr size_t kMaxName = 63;
constexpr size_t kMaxUrl = 199;

void saveStations() {
    noDelay();
    if (!authorized()) return;
    JsonDocument doc(psram_json::allocator());
    if (!psram_json::parse(doc, server->arg("plain")) || !doc.is<JsonArray>()) {
        return server->send(400, "text/plain", "Not a list of stations");
    }
    std::vector<stations::Station> list;
    for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
        stations::Station s;
        s.name = o["name"] | "";
        s.url = o["url"] | "";
        s.name.trim();
        s.url.trim();
        if (s.name.isEmpty() || !(s.url.startsWith("http://") || s.url.startsWith("https://"))) {
            return server->send(400, "text/plain", "Each station needs a name and an http:// or https:// address");
        }
        if (s.name.length() > kMaxName) {
            return server->send(400, "text/plain", "A station's name is too long: " + s.name);
        }
        if (s.url.length() > kMaxUrl) {
            return server->send(400, "text/plain", "A station's address is too long (199 characters at most): " + s.name);
        }
        list.push_back(s);
    }
    {
        Lock lock;
        pendingStations = list;
    }
    if (!request(Action::SetStations)) return server->send(500, "text/plain", "The radio is busy");
    server->send(200, "application/json", "{}");
}

}  // namespace

void addStationRoutes() { server->on("/api/stations", HTTP_POST, saveStations); }

}  // namespace upload_server
