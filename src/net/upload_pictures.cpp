// The upload server's wallpapers: thumbnails, a picture sent from the page,
// and deleting one

#include <SD_MMC.h>
#include <esp_heap_caps.h>

#include "app/settings.h"
#include "app/storage.h"
#include "ui/ui.h"
#include "ui/wallpaper.h"
#include "upload_internal.h"

namespace upload_server {
namespace {

constexpr size_t kMaxNameLength = 24;  // characters, as the page's field

// One picture upload's state, in the server task
struct Upload {
    File thumb, full;
    uint16_t id = 0;
    String name;
    size_t received = 0;
    const char *refused = nullptr;  // why, when it was
    int refusedStatus = 0;          // and the HTTP status that goes with it
    bool failed = false;
} up;

void put16(uint8_t *p, uint16_t v) {
    p[0] = v;
    p[1] = v >> 8;
}

void put32(uint8_t *p, uint32_t v) {
    put16(p, v);
    put16(p + 2, v >> 16);
}

// A wallpaper's thumbnail as a BMP, which browsers show as it is: RGB565
// pixels with their bit masks, rows from the top (a negative height)
void sendThumb() {
    noDelay();
    if (!authorized()) return;
    const int index = server->arg("i").toInt();
    uint16_t id = 0;
    if (index >= wallpaper::designCount()) {
        Lock lock;
        const int i = index - wallpaper::designCount();
        if (i < (int)pictureIds.size()) id = pictureIds[i];
    }
    if (index < 0 || (index >= wallpaper::designCount() && !id)) return server->send(404, "text/plain", "No such wallpaper");

    auto *pixels = static_cast<uint16_t *>(heap_caps_malloc(wallpaper::kThumbBytes, MALLOC_CAP_SPIRAM));
    if (!pixels) return server->send(500, "text/plain", "Out of memory");
    bool ok = true;
    if (id) {
        storage::CardUse use;  // before the file, so it's let go after the file closes
        File f = use ? SD_MMC.open(wallpaper::picturePath(id, true), "r") : File();
        ok = f && f.read(reinterpret_cast<uint8_t *>(pixels), wallpaper::kThumbBytes) == wallpaper::kThumbBytes;
    } else {
        wallpaper::renderDesignThumb(index, pixels);
    }
    if (!ok) {
        heap_caps_free(pixels);
        return server->send(404, "text/plain", "Can't read it");
    }

    uint8_t h[66] = {'B', 'M'};
    put32(h + 2, sizeof(h) + wallpaper::kThumbBytes);
    put32(h + 10, sizeof(h));  // where the pixels start
    put32(h + 14, 40);         // BITMAPINFOHEADER
    put32(h + 18, wallpaper::kThumbSize);
    put32(h + 22, (uint32_t)-wallpaper::kThumbSize);
    put16(h + 26, 1);
    put16(h + 28, 16);
    put32(h + 30, 3);  // BI_BITFIELDS
    put32(h + 34, wallpaper::kThumbBytes);
    put32(h + 54, 0xF800);
    put32(h + 58, 0x07E0);
    put32(h + 62, 0x001F);
    server->sendHeader("Cache-Control", "max-age=86400");  // the page asks again with a new revision
    server->setContentLength(sizeof(h) + wallpaper::kThumbBytes);
    server->send(200, "image/bmp", "");
    server->sendContent(reinterpret_cast<const char *>(h), sizeof(h));
    server->sendContent(reinterpret_cast<const char *>(pixels), wallpaper::kThumbBytes);
    heap_caps_free(pixels);
}

void dropUpload() {
    if (up.thumb) up.thumb.close();
    if (up.full) up.full.close();
    if (up.id && rawCard) {
        SD_MMC.remove(wallpaper::picturePath(up.id, true) + ".part");
        SD_MMC.remove(wallpaper::picturePath(up.id, false) + ".part");
    }
    rawCard.release();
    Lock lock;
    current.active = false;
    rev++;
}

// "%C5%A1" to "š": the page sends the name percent-encoded, as headers are
// ASCII. Cut between characters, never inside one, and without control
// characters (a "%00" among them).
String pictureName(const String &in) {
    String out;
    size_t chars = 0;
    for (size_t i = 0; i < in.length(); i++) {
        uint8_t c = in[i];
        if (c == '%' && i + 2 < in.length()) {
            c = strtol(in.substring(i + 1, i + 3).c_str(), nullptr, 16);
            i += 2;
        }
        if (c < 0x20 || c == 0x7F) continue;
        if ((c & 0xC0) != 0x80 && ++chars > kMaxNameLength) break;  // a character's first byte
        out += (char)c;
    }
    out.trim();
    return out.isEmpty() ? "Picture" : out;
}

// The body is the thumbnail, then the picture, both RGB565 as the radio draws
// them. The code and name come as headers: WebServer reads the query string
// only after the body, so arg() would still hold the last request's.
void receivePicture() {
    if (!server->rawRequest()) return;  // a multipart form's file: not from this page
    HTTPRaw &raw = server->raw();
    switch (raw.status) {
        case RAW_START: {
            up = Upload{};
            rawCard.release();
            if (!codeOk(server->header("X-Code"))) {
                up.refused = "Wrong code";
                up.refusedStatus = 403;
            } else if ((size_t)server->clientContentLength() != kPictureUpload) {
                up.refused = "Not a picture from this page";
                up.refusedStatus = 400;
            } else if (!rawCard.take()) {
                up.refused = "No microSD card in the radio";
                up.refusedStatus = 409;
            }
            if (up.refused) return refuseRaw(up.refusedStatus, up.refused);
            do {
                up.id = 1 + esp_random() % 30000;
            } while (SD_MMC.exists(wallpaper::picturePath(up.id, false)));
            up.name = pictureName(server->header("X-Name"));
            SD_MMC.mkdir("/pictures");
            up.thumb = SD_MMC.open(wallpaper::picturePath(up.id, true) + ".part", FILE_WRITE);
            up.full = SD_MMC.open(wallpaper::picturePath(up.id, false) + ".part", FILE_WRITE);
            if (!up.thumb || !up.full) {
                up.failed = true;
                dropUpload();
                return;
            }
            cancelRequested = false;
            Lock lock;
            current = {true, up.name, 0};
            rev++;
            break;
        }
        case RAW_WRITE: {
            if (up.refused || up.failed) return;
            if (cancelRequested) {
                up.failed = true;
                dropUpload();
                server->client().stop();  // cuts the transfer
                return;
            }
            const uint8_t *data = raw.buf;
            size_t n = raw.currentSize;
            bool ok = true;
            if (up.received < wallpaper::kThumbBytes) {
                const size_t part = min(n, wallpaper::kThumbBytes - up.received);
                ok = up.thumb.write(data, part) == part;
                data += part;
                n -= part;
                up.received += part;
            }
            if (ok && n) {
                ok = up.full.write(data, n) == n;
                up.received += n;
            }
            if (!ok) {
                up.failed = true;
                dropUpload();
                return;
            }
            lastActivity = millis();
            Lock lock;
            current.percent = up.received * 100 / kPictureUpload;
            break;
        }
        case RAW_END: {
            if (up.refused || up.failed) return;
            up.thumb.close();
            up.full.close();
            const String thumb = wallpaper::picturePath(up.id, true), full = wallpaper::picturePath(up.id, false);
            if (up.received != kPictureUpload || !SD_MMC.rename(thumb + ".part", thumb) ||
                !SD_MMC.rename(full + ".part", full)) {
                up.failed = true;
                dropUpload();
                return;
            }
            rawCard.release();  // the UI task takes it from here
            {
                Lock lock;
                pendingId = up.id;
                pendingName = up.name;
                current.active = false;
                rev++;
            }
            if (!request(Action::AddPicture)) {
                up.failed = true;
                // Not taken in time, so never: nothing would list the files,
                // and they'd stay on the card for good
                storage::CardUse use;
                if (use) {
                    SD_MMC.remove(thumb);
                    SD_MMC.remove(full);
                }
            }
            break;
        }
        case RAW_ABORTED:
            up.failed = true;
            dropUpload();
            break;
    }
}

void pictureDone() {
    noDelay();
    // `up` is this request's only after its raw body (a form's leaves the last one's)
    if (!server->rawRequest()) return server->send(400, "text/plain", "Not a picture from this page");
    if (up.refused) return server->send(up.refusedStatus, "text/plain", up.refused);
    if (up.failed) return server->send(500, "text/plain", cancelRequested ? "Cancelled on the radio" : "Couldn't store it");
    server->send(200, "application/json", "{}");
}

void deletePicture() {
    noDelay();
    if (!authorized()) return;
    {
        Lock lock;
        pendingId = server->arg("id").toInt();
    }
    if (!request(Action::DeletePicture)) return server->send(500, "text/plain", "The radio is busy");
    server->send(200, "application/json", "{}");
}

}  // namespace

void addPictureRoutes() {
    server->on("/api/thumb", HTTP_GET, sendThumb);
    server->on("/api/picture", HTTP_POST, pictureDone, receivePicture);
    server->on("/api/delete", HTTP_POST, deletePicture);
}

// ----- UI task -----

void applyPicture(Action action, uint16_t id, const String &name) {
    const int before = wallpaper::current();
    switch (action) {
        case Action::AddPicture:
            wallpaper::addPicture(id, name);
            break;
        case Action::DeletePicture:
            if (wallpaper::isPicture(wallpaper::current())) {
                const std::vector<wallpaper::Picture> pics = wallpaper::pictures();
                const int i = wallpaper::current() - wallpaper::designCount();
                if (pics[i].id == id) wallpaper::choose(settings::Settings{}.wallpaper);
            }
            wallpaper::removePicture(id);
            break;
        default:
            break;
    }
    // Home shows the one in use, and keeps it till told (it was the one deleted)
    if (wallpaper::current() != before) ui::applySettings();
}

}  // namespace upload_server
