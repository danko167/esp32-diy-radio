// The core: the server's task, the code, the hand-over to the UI task and
// /api/info. Each feature's routes are in its own file (upload_internal.h).

#include "upload_server.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <SD_MMC.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/idf_additions.h>
#include <mdns.h>

#include <vector>

#include "app/alarms.h"
#include "app/psram_json.h"
#include "audio/player.h"
#include "audio/recorder.h"
#include "app/settings.h"
#include "app/storage.h"
#include "net/bounded_server.h"
#include "net/stations.h"
#include "ui/theme.h"
#include "ui/wallpaper.h"
#include "upload_internal.h"
#include "upload_page.h"  // web/upload.html, gzipped by tools/embed_web.py

namespace upload_server {
namespace {

constexpr int kMaxWrongCodes = 10;
constexpr uint32_t kLockoutMs = 60000;   // after that many wrong codes in a row
constexpr int kMaxWrongPerRun = 30;      // then it stops
constexpr uint32_t kApplyWaitMs = 3000;  // for the UI task to take a change
// For all but the raw uploads: an 8 KB face, percent-encoded, is up to 24 KB
constexpr size_t kMaxBody = 32 * 1024;

// The server runs in its own task, so a picture arriving never stalls the
// screen. What the UI task owns (pictures, favourites) it hands over through
// `pending`, and the UI task answers through `info`; `mutex` guards both,
// and the transfer.
TaskHandle_t task = nullptr;
volatile bool served = false;  // the task is done and waits to be deleted
volatile bool stopping = false;
SemaphoreHandle_t mutex = nullptr;

char codeText[5] = "";
int wrongCodes = 0;
uint32_t lockedSince = 0;
int wrongInRun = 0;                        // since start(), never reset by a right code
const char *volatile stopReason = nullptr;  // why it stopped by itself, for the radio's page

Action pending = Action::None;
bool applied = false;
bool appliedOk = false;  // and whether it worked (the card took the rename)

String info;  // /api/info's answer
// The revisions it was built from, and the accent it gives the page
uint32_t infoFor[4] = {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX};

}  // namespace

// Shared with the features' files
BoundedServer *server = nullptr;
volatile uint32_t lastActivity = 0;
uint32_t rev = 0;

uint16_t pendingId = 0;
String pendingName;
std::vector<stations::Station> pendingStations;

std::vector<uint16_t> pictureIds;

Transfer current;
volatile bool cancelRequested = false;

// The card, for the upload being received (pictures and face files, one at a
// time): taken at its start, let go when its files are closed. WebServer
// reads a raw body within one handleClient(), and ends it with RAW_END or
// RAW_ABORTED (a client gone quiet or cut), so it's always let go.
storage::CardUse rawCard{false};

Lock::Lock() { xSemaphoreTake(mutex, portMAX_DELAY); }
Lock::~Lock() { xSemaphoreGive(mutex); }

// ----- Server task -----

// Each reply in one go: with Nagle's algorithm on, the radio waits for an
// ack that Windows delays by up to 200 ms, every few packets. Only on a live
// connection: the server's client is empty between requests.
void noDelay() { server->client().setNoDelay(true); }

// Its Content-Length may say 2 GB, which would all be read to be thrown away.
// Closed, the read ends there (RAW_ABORTED) and the done handler isn't run.
// Without a body the done handler answers, as the read ends anyway.
void refuseRaw(int status, const char *why) {
    if (server->clientContentLength() <= 0) return;
    noDelay();
    server->send(status, "text/plain", why);
    server->client().stop();
}

void sendFile(File &f, const char *type) {
    constexpr size_t kChunk = 4096;
    auto *buf = static_cast<uint8_t *>(heap_caps_malloc(kChunk, MALLOC_CAP_SPIRAM));
    if (!buf) return server->send(500, "text/plain", "Out of memory");
    server->setContentLength(f.size());
    server->send(200, type, "");
    while (!stopping) {
        const size_t n = f.read(buf, kChunk);
        if (!n || server->client().write(buf, n) != n) break;
        lastActivity = millis();  // a long recording is activity all along
    }
    heap_caps_free(buf);
}

bool codeOk(const String &given) {
    if (stopping) return false;
    if (lockedSince && millis() - lockedSince < kLockoutMs) return false;
    lockedSince = 0;
    if (given != codeText) {
        if (++wrongCodes >= kMaxWrongCodes) {
            lockedSince = millis();
            wrongCodes = 0;
        }
        // The lockout only slows guessing down; this ends it, at 30 tries of 9000
        if (++wrongInRun >= kMaxWrongPerRun) {
            stopReason = "Stopped: too many wrong codes";
            stop();
        }
        return false;
    }
    wrongCodes = 0;
    lastActivity = millis();  // each request starts the idle time again
    return true;
}

// From the query string: fine for requests without a raw body (see receivePicture)
bool authorized() {
    if (codeOk(server->arg("c"))) return true;
    server->send(403, "text/plain", stopping      ? "Too many wrong codes: start it again on the radio"
                                    : lockedSince ? "Too many wrong codes, wait a minute"
                                                  : "Wrong code");
    return false;
}

// Hands a change to the UI task and waits for it to be done. Not taken in
// time, it's called off, so it never happens after the page heard it failed;
// once taken, it's waited for.
bool request(Action action) {
    {
        Lock lock;
        pending = action;
        applied = false;
    }
    for (const uint32_t start = millis();; vTaskDelay(pdMS_TO_TICKS(10))) {
        Lock lock;
        if (applied) return appliedOk;
        if (pending == action && (stopping || millis() - start >= kApplyWaitMs)) {
            pending = Action::None;
            return false;
        }
    }
}

namespace {

void sendPage() {
    noDelay();
    server->sendHeader("Content-Encoding", "gzip");
    server->send_P(200, "text/html; charset=utf-8", reinterpret_cast<const char *>(kUploadPage), kUploadPageSize);
}

void sendInfo() {
    noDelay();
    if (!authorized()) return;
    String copy;
    {
        Lock lock;
        copy = info;
    }
    server->sendHeader("Cache-Control", "no-store");
    server->send(200, "application/json", copy);
}

void serve(void *) {
    while (!stopping) {
        server->handleClient();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    server->stop();
    rawCard.release();  // already, but nothing half-done may keep the card
    // Its stack is in PSRAM, and such a task can't delete itself: loop() does
    served = true;
    vTaskSuspend(nullptr);
}

// ----- UI task -----

void buildInfo() {
    JsonDocument doc(psram_json::allocator());
    const bool card = storage::hasCard();
    doc["card"] = card;
    if (card) {
        doc["free"] = SD_MMC.totalBytes() - SD_MMC.usedBytes();
        doc["total"] = SD_MMC.totalBytes();
    }
    doc["pictureBytes"] = kPictureUpload;
    char accent[8];
    snprintf(accent, sizeof(accent), "#%06lx", (unsigned long)theme::accent());
    doc["accent"] = accent;  // the page takes the radio's colour
    doc["designs"] = wallpaper::designCount();
    doc["current"] = wallpaper::current();
    doc["revision"] = wallpaper::revision();
    JsonArray walls = doc["wallpapers"].to<JsonArray>();
    std::vector<uint16_t> ids;
    for (int i = 0; i < wallpaper::designCount(); i++) walls.add<JsonObject>()["name"] = wallpaper::name(i);
    for (const wallpaper::Picture &p : wallpaper::pictures()) {
        JsonObject o = walls.add<JsonObject>();
        o["name"] = p.name;
        o["id"] = p.id;
        ids.push_back(p.id);
    }
    JsonArray list = doc["stations"].to<JsonArray>();
    for (const stations::Station &s : stations::favourites()) {
        JsonObject o = list.add<JsonObject>();
        o["name"] = s.name;
        o["url"] = s.url;
    }
    String text;
    psram_json::serialize(doc, text);
    Lock lock;
    info = text;
    pictureIds = ids;
}

void apply() {
    // Taken with its parameters under the lock, then worked on from the copies
    Action action;
    uint16_t id;
    String name;
    std::vector<stations::Station> list;
    {
        Lock lock;
        if (pending == Action::None) return;
        action = pending;
        pending = Action::None;
        id = pendingId;
        name = pendingName;
        list = pendingStations;
    }
    bool ok = true;  // false when the card doesn't take a file, so the page hears it
    switch (action) {
        case Action::AddPicture:
        case Action::DeletePicture:
            applyPicture(action, id, name);
            break;
        case Action::SetStations:
            stations::setFavourites(list);
            break;
        case Action::SaveFace:
        case Action::SaveFaceAsset:
        case Action::FaceDone:
        case Action::DeleteFace:
            ok = applyFace(action, id, name);
            break;
        case Action::None:
            return;
    }
    buildInfo();  // before answering, so the page sees the change
    Lock lock;
    applied = true;
    appliedOk = ok;
    rev++;
}

}  // namespace

void start() {
    if (server || WiFi.status() != WL_CONNECTED) return;  // also while the last one is still stopping
    if (!mutex) mutex = xSemaphoreCreateMutex();
    // Recording has the sound chip and the card to itself, and an alarm the
    // sound (one starting stops the server, see alarms.cpp)
    const char *refused = recorder::recording() ? "Not while recording: stop the Recorder first"
                          : alarms::ringing()   ? "Not while an alarm rings"
                                                : nullptr;
    if (refused) {
        stopReason = refused;
        Lock lock;
        rev++;
        return;
    }
    snprintf(codeText, sizeof(codeText), "%04lu", (unsigned long)(1000 + esp_random() % 9000));
    lastActivity = millis();
    wrongCodes = 0;
    lockedSince = 0;
    wrongInRun = 0;
    stopReason = nullptr;
    infoFor[0] = UINT32_MAX;
    {
        Lock lock;  // nothing the last run left over
        pending = Action::None;
        applied = false;
    }
    current = Transfer{};

    MDNS.begin("diy-radio");  // already running when OTA is on; then this changes nothing
    MDNS.addService("http", "tcp", 80);
    server = new BoundedServer(80, kMaxBody);
    const char *headers[] = {"X-Code", "X-Name", "X-Face", "X-File"};
    server->collectHeaders(headers, 4);
    server->on("/", HTTP_GET, sendPage);
    server->on("/api/info", HTTP_GET, sendInfo);
    addPictureRoutes();
    addStationRoutes();
    addFaceRoutes();
    addRecordingRoutes();
    server->onNotFound([] {
        noDelay();
        server->send(404, "text/plain", "Not here");
    });
    server->begin();
    stopping = false;
    served = false;
    // Uploads have the radio to themselves: no sound meanwhile (an alarm stops
    // the server instead, see alarms.cpp)
    player::hold("Off while the upload page is on");
    // The stack in PSRAM: 8 KB more internal RAM. Fine, as this task only
    // writes to the card, never to internal flash (which turns PSRAM off).
    if (xTaskCreatePinnedToCoreWithCaps(serve, "upload", 8192, nullptr, 1, &task, 0, MALLOC_CAP_SPIRAM) != pdPASS) {
        Serial.println("upload: no memory for its task");
        delete server;
        server = nullptr;
        player::hold(nullptr);
        return;
    }
    Serial.printf("upload: on at %s, code %s\n", address().c_str(), codeText);
    Lock lock;  // the task is running
    rev++;
}

// The task ends after the request it's in; loop() then finishes the job
void stop(const char *why) {
    if (!server || stopping) return;
    if (why) stopReason = why;
    // Sound may start again right away: whatever stops the server needs it (an alarm)
    player::hold(nullptr);
    cancelRequested = true;  // a picture on its way is cut
    stopping = true;
    server->cancel();  // and whatever request is being read
    Lock lock;  // also from the server task (codeOk)
    rev++;
}

void loop() {
    if (!server) return;
    if (stopping) {
        if (!served) return;
        vTaskDeleteWithCaps(task);
        task = nullptr;
        delete server;
        server = nullptr;
        mdns_service_remove("_http", "_tcp");
        Serial.println("upload: off");
        rev++;
        return;
    }
    apply();
    const uint32_t revs[4] = {wallpaper::revision(), stations::revision(), storage::cardRevision(),
                              theme::accent()};
    if (memcmp(revs, infoFor, sizeof(revs)) != 0) {
        memcpy(infoFor, revs, sizeof(revs));
        buildInfo();
    }
    if (WiFi.status() != WL_CONNECTED || secondsLeft() == 0) stop();
}

bool running() { return server && !stopping; }
const char *stoppedWhy() { return running() ? nullptr : stopReason; }
const char *code() { return codeText; }

uint32_t secondsLeft() {
    const uint32_t idle = (millis() - lastActivity) / 1000;
    const uint32_t limit = settings::get().uploadIdleMinutes * 60;
    // Never in a request: stopped, it'd go on reading the card (a download)
    // with the sound let go, and the Recorder free to start
    if (server && server->busy()) return limit;
    return idle >= limit ? 0 : limit - idle;
}

String address() { return "http://" + WiFi.localIP().toString(); }

Transfer transfer() {
    if (!mutex) return {};
    Lock lock;
    return current;
}

void cancelTransfer() { cancelRequested = true; }

uint32_t revision() {
    if (!mutex) return rev;
    Lock lock;
    return rev;
}

}  // namespace upload_server
