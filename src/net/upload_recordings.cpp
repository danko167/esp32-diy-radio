// The upload server's recordings: list, download and delete

#include <ArduinoJson.h>
#include <SD_MMC.h>

#include "app/psram_json.h"
#include "app/storage.h"
#include "audio/recorder.h"
#include "upload_internal.h"

namespace upload_server {
namespace {

// The Recorder's files: what the radio's list shows, from the card directly
void sendRecordings() {
    noDelay();
    if (!authorized()) return;
    JsonDocument doc(psram_json::allocator());
    doc["card"] = storage::hasCard();
    JsonArray list = doc["recordings"].to<JsonArray>();
    for (const recorder::Recording &r : recorder::list()) {
        JsonObject o = list.add<JsonObject>();
        o["name"] = r.name;
        o["bytes"] = r.bytes;
        o["seconds"] = r.seconds;
    }
    String text;
    psram_json::serialize(doc, text);
    server->sendHeader("Cache-Control", "no-store");
    server->send(200, "application/json", text);
}

void sendRecording() {
    noDelay();
    if (!authorized()) return;
    const String name = server->arg("f");
    if (!recorder::isName(name)) return server->send(400, "text/plain", "No such recording");
    storage::CardUse use;  // till the file's been sent and closed (it's declared after)
    if (!use) return server->send(409, "text/plain", "No microSD card in the radio");
    File f = SD_MMC.open(recorder::path(name), "r");
    if (!f) return server->send(404, "text/plain", "No such recording");
    server->sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    server->sendHeader("Cache-Control", "no-store");
    sendFile(f, "audio/wav");
}

void deleteRecording() {
    noDelay();
    if (!authorized()) return;
    if (!recorder::remove(server->arg("f"))) return server->send(404, "text/plain", "No such recording");
    server->send(200, "application/json", "{}");
}

}  // namespace

void addRecordingRoutes() {
    server->on("/api/recordings", HTTP_GET, sendRecordings);
    server->on("/api/recording", HTTP_GET, sendRecording);
    server->on("/api/recording/delete", HTTP_POST, deleteRecording);
}

}  // namespace upload_server
