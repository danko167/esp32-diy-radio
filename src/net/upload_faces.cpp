// The upload server's clock faces
//
// Faces made on the page's editor (design/src/faceFile.ts), one folder each
// on the card (ui/card_faces.h): /faces/<id>/face.json, and maybe bg.bin
// (480 x 480 RGB565, little-endian, like a wallpaper) and fonts (<name>.bin,
// LVGL binary fonts as lv_font_conv --format bin makes them, up to 256 KB).
// Ids are 1..65535, a new face gets the highest one plus 1. All need the code.
//   GET  /api/faces?c=CODE  {"faces":[{"id":3,"name":"Corner","face":{the face file},
//        "bg":true,"fonts":["MyFont.bin"]}],"builtIn":["Digital",...],"card":true,"free":bytes}
//   GET  /api/faceasset?c=CODE&id=3&file=bg.bin  the file as it is
//   POST /api/face?c=CODE&id=3 (0 for a new face), form field "face" = the face file's JSON:
//        writes face.json, deletes the assets it no longer uses; {"id":3}. 400 when it's
//        not a format 1 face, over 8 KB or its name isn't 1..24 characters; 404 for no such id.
//   POST /api/faceasset, headers X-Code, X-Face (the id) and X-File ("bg.bin", exactly
//        460800 bytes, or [A-Za-z0-9_-]{1,24}.bin, up to 262144), the body the file: {}
//   POST /api/face/done?c=CODE&id=3  after the face and its assets: the radio takes it
//   POST /api/face/delete?c=CODE&id=3  deletes the folder; if in use, the radio shows Digital
// No card: 409. The radio's own task (apply()) puts files in place and reads them;
// this one writes new files beside them (.part, .tmp) and reads.

#include <ArduinoJson.h>
#include <SD_MMC.h>

#include <algorithm>
#include <vector>

#include "app/psram_json.h"
#include "app/storage.h"
#include "ui/card_faces.h"
#include "upload_internal.h"

namespace upload_server {
namespace {

uint16_t faceId() {
    const long id = server->arg("id").toInt();
    return id > 0 && id <= UINT16_MAX ? id : 0;
}

bool faceExists(uint16_t id) { return id && SD_MMC.exists(card_faces::path(id, "face.json")); }

std::vector<uint16_t> faceIds() {
    std::vector<uint16_t> ids;
    File dir = SD_MMC.open("/faces");
    if (!dir || !dir.isDirectory()) return ids;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        const long id = String(f.name()).toInt();
        if (f.isDirectory() && id > 0 && id <= UINT16_MAX) ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<String> filesOf(uint16_t id) {
    std::vector<String> names;
    File dir = SD_MMC.open(card_faces::folder(id));
    if (!dir || !dir.isDirectory()) return names;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (!f.isDirectory()) names.push_back(f.name());
    }
    return names;
}

void sendFaces() {
    noDelay();
    if (!authorized()) return;
    JsonDocument doc(psram_json::allocator());
    JsonArray list = doc["faces"].to<JsonArray>();
    storage::CardUse use;
    const bool card = (bool)use;
    if (card) {
        for (uint16_t id : faceIds()) {
            File f = SD_MMC.open(card_faces::path(id, "face.json"), "r");
            if (!f || f.size() > card_faces::kMaxJson) continue;
            JsonDocument face(psram_json::allocator());
            const char *why;
            if (!psram_json::parse(face, f) || !card_faces::isFace(face.as<JsonVariantConst>(), why)) continue;
            JsonObject o = list.add<JsonObject>();
            o["id"] = id;
            o["name"] = face["name"];
            o["face"] = face;
            bool bg = false;
            JsonArray fonts = o["fonts"].to<JsonArray>();
            for (const String &name : filesOf(id)) {
                if (name == "bg.bin") {
                    bg = true;
                } else if (card_faces::isAssetName(name.c_str())) {
                    fonts.add(name);
                }
            }
            o["bg"] = bg;
        }
    }
    JsonArray builtIn = doc["builtIn"].to<JsonArray>();
    for (const char *name : card_faces::kBuiltIn) builtIn.add(name);
    doc["card"] = card;
    if (card) doc["free"] = SD_MMC.totalBytes() - SD_MMC.usedBytes();
    use.release();
    String text;
    psram_json::serialize(doc, text);
    server->sendHeader("Cache-Control", "no-store");
    server->send(200, "application/json", text);
}

void sendFaceAsset() {
    noDelay();
    if (!authorized()) return;
    const String file = server->arg("file");
    storage::CardUse use;  // till the file's been sent and closed (it's declared after)
    if (!use) return server->send(409, "text/plain", "No microSD card in the radio");
    if (!faceId() || !card_faces::isAssetName(file.c_str())) return server->send(400, "text/plain", "No such file");
    File f = SD_MMC.open(card_faces::path(faceId(), file.c_str()), "r");
    if (!f) return server->send(404, "text/plain", "No such file");
    server->sendHeader("Cache-Control", "no-store");
    sendFile(f, "application/octet-stream");
}

void saveFace() {
    noDelay();
    if (!authorized()) return;
    const String &json = server->arg("face");
    if (json.length() > card_faces::kMaxJson) return server->send(400, "text/plain", "The face is over 8 KB");
    JsonDocument doc(psram_json::allocator());
    const char *why = "Not JSON";
    if (!psram_json::parse(doc, json) || !card_faces::isFace(doc.as<JsonVariantConst>(), why)) {
        return server->send(400, "text/plain", why);
    }
    storage::CardUse use;
    if (!use) return server->send(409, "text/plain", "No microSD card in the radio");
    uint16_t id = faceId();
    if (server->arg("id") == "0") {
        const std::vector<uint16_t> ids = faceIds();
        if (!ids.empty() && ids.back() == UINT16_MAX) return server->send(500, "text/plain", "No room for more faces");
        id = ids.empty() ? 1 : ids.back() + 1;
        SD_MMC.mkdir("/faces");
        SD_MMC.mkdir(card_faces::folder(id));
    } else if (!faceExists(id)) {
        return server->send(404, "text/plain", "No such face");
    }
    // Next to it, so a cut never leaves half a file; the radio's task puts it in place
    File f = SD_MMC.open(card_faces::path(id, "face.json.tmp"), FILE_WRITE);
    const bool written = f && f.print(json) == json.length();
    if (f) f.close();
    use.release();
    if (!written) return server->send(500, "text/plain", "Couldn't store it");
    {
        Lock lock;
        pendingId = id;
    }
    if (!request(Action::SaveFace)) return server->send(500, "text/plain", "The radio is busy");
    server->send(200, "application/json", String("{\"id\":") + id + "}");
}

// One face asset upload's state, in the server task
struct AssetUpload {
    File file;
    uint16_t id = 0;
    String name;
    size_t expected = 0, received = 0;
    const char *refused = nullptr;
    int refusedStatus = 0;
    bool failed = false;
} asset;

void dropAsset() {
    if (asset.file) asset.file.close();
    if (asset.id && rawCard) SD_MMC.remove(card_faces::path(asset.id, (asset.name + ".part").c_str()));
    rawCard.release();
}

// Like receivePicture(): the code, face and file come as headers
void receiveFaceAsset() {
    if (!server->rawRequest()) return;  // a multipart form's file: not from this page
    HTTPRaw &raw = server->raw();
    switch (raw.status) {
        case RAW_START: {
            asset = AssetUpload{};
            rawCard.release();
            const String name = server->header("X-File");
            const long id = server->header("X-Face").toInt();
            const size_t size = server->clientContentLength();
            const bool bg = name == "bg.bin";
            if (!codeOk(server->header("X-Code"))) {
                asset.refused = "Wrong code";
                asset.refusedStatus = 403;
            } else if (!rawCard.take()) {
                asset.refused = "No microSD card in the radio";
                asset.refusedStatus = 409;
            } else if (!card_faces::isAssetName(name.c_str())) {
                asset.refused = "Not a face's file name";
                asset.refusedStatus = 400;
            } else if (id <= 0 || id > UINT16_MAX || !faceExists(id)) {
                asset.refused = "No such face";
                asset.refusedStatus = 404;
            } else if (bg ? size != card_faces::kBackgroundBytes : !size || size > card_faces::kMaxFont) {
                asset.refused = bg ? "Not a 480 x 480 picture" : "A font is up to 256 KB";
                asset.refusedStatus = 400;
            }
            if (asset.refused) {
                rawCard.release();
                return refuseRaw(asset.refusedStatus, asset.refused);
            }
            asset.id = id;
            asset.name = name;
            asset.expected = size;
            asset.file = SD_MMC.open(card_faces::path(id, (name + ".part").c_str()), FILE_WRITE);
            if (!asset.file) {
                asset.failed = true;
                dropAsset();
            }
            cancelRequested = false;
            break;
        }
        case RAW_WRITE:
            if (asset.refused || asset.failed) return;
            if (cancelRequested) {  // the server stopping
                asset.failed = true;
                dropAsset();
                server->client().stop();
                return;
            }
            if (asset.file.write(raw.buf, raw.currentSize) != raw.currentSize) {
                asset.failed = true;
                dropAsset();
                return;
            }
            asset.received += raw.currentSize;
            lastActivity = millis();
            break;
        case RAW_END:
            if (asset.refused || asset.failed) return;
            asset.file.close();
            if (asset.received != asset.expected) {
                asset.failed = true;
                dropAsset();
                return;
            }
            rawCard.release();  // the UI task puts it in place
            {
                Lock lock;
                pendingId = asset.id;
                pendingName = asset.name;
            }
            if (!request(Action::SaveFaceAsset)) asset.failed = true;
            break;
        case RAW_ABORTED:
            asset.failed = true;
            dropAsset();
            break;
    }
}

void faceAssetDone() {
    noDelay();
    // `asset` is this request's only after its raw body (a form's leaves the last one's)
    if (!server->rawRequest()) return server->send(400, "text/plain", "Not a face's file from this page");
    if (asset.refused) return server->send(asset.refusedStatus, "text/plain", asset.refused);
    if (asset.failed) return server->send(500, "text/plain", "Couldn't store it");
    server->send(200, "application/json", "{}");
}

// Done and delete: handed to the radio's task as they are
void faceAction(Action action) {
    noDelay();
    if (!authorized()) return;
    if (!storage::hasCard()) return server->send(409, "text/plain", "No microSD card in the radio");
    if (!faceId()) return server->send(404, "text/plain", "No such face");
    {
        Lock lock;
        pendingId = faceId();
    }
    if (!request(action)) return server->send(500, "text/plain", "The radio is busy");
    server->send(200, "application/json", "{}");
}

}  // namespace

void addFaceRoutes() {
    server->on("/api/faces", HTTP_GET, sendFaces);
    server->on("/api/faceasset", HTTP_GET, sendFaceAsset);
    server->on("/api/faceasset", HTTP_POST, faceAssetDone, receiveFaceAsset);
    server->on("/api/face", HTTP_POST, saveFace);
    server->on("/api/face/done", HTTP_POST, [] { faceAction(Action::FaceDone); });
    server->on("/api/face/delete", HTTP_POST, [] { faceAction(Action::DeleteFace); });
}

// ----- UI task -----

bool applyFace(Action action, uint16_t id, const String &name) {
    bool ok = true;
    switch (action) {
        case Action::SaveFace: {
            const String json = card_faces::path(id, "face.json");
            SD_MMC.remove(json);
            if (!SD_MMC.rename(json + ".tmp", json)) {
                ok = false;
                break;
            }
            // What it no longer uses goes, and what a cut upload left
            JsonDocument doc(psram_json::allocator());
            if (!card_faces::load(id, doc)) break;
            const std::vector<String> keep = card_faces::assets(doc.as<JsonVariantConst>());
            for (const String &file : filesOf(id)) {
                if (file != "face.json" && std::find(keep.begin(), keep.end(), file) == keep.end()) {
                    SD_MMC.remove(card_faces::path(id, file.c_str()));
                }
            }
            break;
        }
        case Action::SaveFaceAsset: {
            const String path = card_faces::path(id, name.c_str());
            SD_MMC.remove(path);
            ok = SD_MMC.rename(path + ".part", path);
            break;
        }
        case Action::FaceDone:
            card_faces::reload(id);  // the clock screen and Settings follow its revision
            break;
        case Action::DeleteFace:
            card_faces::remove(id);
            break;
        default:
            break;
    }
    return ok;
}

}  // namespace upload_server
