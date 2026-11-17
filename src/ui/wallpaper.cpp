#include "wallpaper.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>

#include <memory>

#include "app/psram_json.h"
#include "app/settings.h"
#include "app/storage.h"
#include "app/watchdog.h"
#include "hal/pins.h"

namespace wallpaper {
namespace {

// Position and radius are fractions of the screen size
struct Blob {
    float x, y, radius;
    uint32_t rgb;
    float strength;
};

struct Design {
    const char *name;
    Blob blobs[3];
    int blobCount;
};

const Design kDesigns[] = {
    {"Black", {}, 0},
    {"Aurora",
     {{0.15f, 0.05f, 0.60f, 0x00C8A0, 0.85f},
      {0.85f, 0.15f, 0.55f, 0x6A3DE8, 0.90f},
      {0.50f, -0.10f, 0.45f, 0x0A7AFF, 0.60f}},
     3},
    {"Sunset",
     {{0.20f, 1.00f, 0.60f, 0xFF5E3A, 0.85f},
      {0.85f, 0.95f, 0.55f, 0xC21F7A, 0.90f},
      {0.55f, 1.15f, 0.50f, 0xFFA030, 0.70f}},
     3},
    {"Ocean",
     {{0.00f, 0.65f, 0.70f, 0x0040B0, 0.90f},
      {1.00f, 0.30f, 0.60f, 0x0090C8, 0.70f},
      {0.60f, 1.05f, 0.50f, 0x281488, 0.80f}},
     3},
    {"Nebula",
     {{0.30f, 0.35f, 0.45f, 0xB030C0, 0.80f},
      {0.72f, 0.68f, 0.50f, 0x3040D0, 0.80f},
      {0.88f, 0.12f, 0.30f, 0xFF4080, 0.50f}},
     3},
    {"Ember",
     {{0.50f, 1.08f, 0.70f, 0xD03010, 0.90f},
      {0.12f, 0.85f, 0.35f, 0xFF8020, 0.50f},
      {0.90f, 0.90f, 0.30f, 0xA01040, 0.50f}},
     3},
};
constexpr int kCount = sizeof(kDesigns) / sizeof(kDesigns[0]);

// 4x4 ordered dither: smooth gradients would band badly in 16-bit colour
const uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

void render(const Design &d, int w, int h, uint16_t *out) {
    struct Prepared {
        float x, y, invR2, r, g, b;
    } blobs[3];
    for (int i = 0; i < d.blobCount; i++) {
        const Blob &b = d.blobs[i];
        blobs[i] = {b.x, b.y, 1.0f / (b.radius * b.radius),
                    b.strength * ((b.rgb >> 16) & 0xFF) / 255.0f,
                    b.strength * ((b.rgb >> 8) & 0xFF) / 255.0f,
                    b.strength * (b.rgb & 0xFF) / 255.0f};
    }

    for (int y = 0; y < h; y++) {
        if (y % 64 == 0) watchdog::feed();  // a full screen takes a while
        const float fy = (y + 0.5f) / h;
        for (int x = 0; x < w; x++) {
            const float fx = (x + 0.5f) / w;
            float r = 0, g = 0, b = 0;
            for (int i = 0; i < d.blobCount; i++) {
                const Prepared &p = blobs[i];
                const float dx = fx - p.x, dy = fy - p.y;
                const float t = 1.0f + 3.0f * (dx * dx + dy * dy) * p.invR2;
                const float f = 1.0f / (t * t);  // soft falloff with a long tail
                r += f * p.r;
                g += f * p.g;
                b += f * p.b;
            }
            const float dither = (kBayer[y & 3][x & 3] + 0.5f) / 16.0f;
            const int r5 = constrain((int)(r * 31.0f + dither), 0, 31);
            const int g6 = constrain((int)(g * 63.0f + dither), 0, 63);
            const int b5 = constrain((int)(b * 31.0f + dither), 0, 31);
            *out++ = (r5 << 11) | (g6 << 5) | b5;
        }
    }
}

void initDsc(lv_image_dsc_t &dsc, int w, int h, uint16_t *pixels) {
    dsc = {};
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    dsc.header.w = w;
    dsc.header.h = h;
    dsc.header.stride = w * 2;
    dsc.data_size = w * h * 2;
    dsc.data = reinterpret_cast<const uint8_t *>(pixels);
}

uint16_t *allocPixels(int w, int h) {
    return static_cast<uint16_t *>(heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM));
}

lv_image_dsc_t full;
uint16_t *fullPixels = nullptr;
int fullKey = -1;  // what's in fullPixels: a design's index, or kPictureKey + id

lv_image_dsc_t thumbs[kCount];
uint16_t *thumbPixels[kCount] = {};

// ----- Pictures -----

constexpr char kDir[] = "/pictures";
constexpr char kIndex[] = "/pictures/index.json";
constexpr int kPictureKey = 1000;
constexpr uint8_t kPictureChoice = 255;  // settings' wallpaper value for "a picture"

struct Loaded {
    Picture picture;
    lv_image_dsc_t thumb{};
    uint16_t *thumbPixels = nullptr;
    ~Loaded() { heap_caps_free(thumbPixels); }
};
// Held by pointer: the settings picker's images point at `thumb`, so it
// mustn't move when the list grows. One that goes while its thumbnail may
// still be shown waits in `gone` until the picker says nothing does.
std::vector<std::unique_ptr<Loaded>> pictureList, gone;
uint32_t loadedCard = UINT32_MAX;  // storage::cardRevision() the list is from
uint32_t rev = 0;

void retire(std::unique_ptr<Loaded> l) {
    if (l->thumbPixels) gone.push_back(std::move(l));
}

void freeList() {
    for (auto &l : pictureList) retire(std::move(l));
    pictureList.clear();
}

void saveIndex() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto &l : pictureList) {
        JsonObject o = arr.add<JsonObject>();
        o["id"] = l->picture.id;
        o["name"] = l->picture.name;
    }
    const String tmp = String(kIndex) + ".tmp";
    File f = SD_MMC.open(tmp, FILE_WRITE);
    if (!f) return;
    psram_json::serialize(doc, f);
    f.close();
    SD_MMC.remove(kIndex);
    SD_MMC.rename(tmp, kIndex);
}

// The list follows the card: reloaded when one comes or goes
void sync() {
    if (storage::cardRevision() == loadedCard) return;
    loadedCard = storage::cardRevision();
    freeList();
    rev++;
    if (fullKey >= kPictureKey) fullKey = -1;
    if (!storage::hasCard()) return;
    File f = SD_MMC.open(kIndex, "r");
    if (!f) return;
    JsonDocument doc;
    if (!psram_json::parse(doc, f)) return;
    for (JsonObjectConst o : doc.as<JsonArrayConst>()) {
        auto l = std::make_unique<Loaded>();
        l->picture = {o["id"] | (uint16_t)0, o["name"] | "Picture"};
        if (l->picture.id) pictureList.push_back(std::move(l));
    }
}

Loaded *pictureAt(int index) {
    sync();
    const int i = index - kCount;
    return i >= 0 && i < (int)pictureList.size() ? pictureList[i].get() : nullptr;
}

bool readInto(const String &path, uint16_t *pixels, size_t bytes) {
    watchdog::feed();  // a full picture from a slow card takes a while
    File f = SD_MMC.open(path, "r");
    return f && f.size() == bytes && f.read(reinterpret_cast<uint8_t *>(pixels), bytes) == bytes;
}

}  // namespace

int count() {
    sync();
    return kCount + pictureList.size();
}

int designCount() { return kCount; }

const char *name(int index) {
    if (const Loaded *l = pictureAt(index)) return l->picture.name.c_str();
    return kDesigns[constrain(index, 0, kCount - 1)].name;
}

bool isPicture(int index) { return pictureAt(index) != nullptr; }

const lv_image_dsc_t *image(int index) {
    const Loaded *l = pictureAt(index);
    if (!l) index = constrain(index, 0, kCount - 1);
    if (!l && kDesigns[index].blobCount == 0) return nullptr;
    const int key = l ? kPictureKey + l->picture.id : index;
    if (key == fullKey) return &full;

    if (!fullPixels) {
        fullPixels = allocPixels(LCD_WIDTH, LCD_HEIGHT);
        if (!fullPixels) return nullptr;
        initDsc(full, LCD_WIDTH, LCD_HEIGHT, fullPixels);
    }
    const uint32_t start = millis();
    if (l) {
        fullKey = -1;
        if (!readInto(picturePath(l->picture.id, false), fullPixels, kPictureBytes)) return nullptr;
        Serial.printf("wallpaper: loaded %s in %lu ms\n", l->picture.name.c_str(), millis() - start);
    } else {
        render(kDesigns[index], LCD_WIDTH, LCD_HEIGHT, fullPixels);
        Serial.printf("wallpaper: rendered %s in %lu ms\n", kDesigns[index].name, millis() - start);
    }
    // Raw RGB565 is drawn straight from the buffer (no image cache), so
    // invalidating the image object is enough to show the new pixels
    fullKey = key;
    return &full;
}

const lv_image_dsc_t *thumbnail(int index) {
    if (Loaded *l = pictureAt(index)) {
        if (!l->thumbPixels) {
            l->thumbPixels = allocPixels(kThumbSize, kThumbSize);
            if (!l->thumbPixels) return nullptr;
            if (!readInto(picturePath(l->picture.id, true), l->thumbPixels, kThumbBytes)) {
                memset(l->thumbPixels, 0, kThumbBytes);
            }
            initDsc(l->thumb, kThumbSize, kThumbSize, l->thumbPixels);
        }
        return &l->thumb;
    }
    index = constrain(index, 0, kCount - 1);
    if (!thumbPixels[index]) {
        thumbPixels[index] = allocPixels(kThumbSize, kThumbSize);
        if (!thumbPixels[index]) return nullptr;
        render(kDesigns[index], kThumbSize, kThumbSize, thumbPixels[index]);
        initDsc(thumbs[index], kThumbSize, kThumbSize, thumbPixels[index]);
    }
    return &thumbs[index];
}

void freeThumbnails() {
    gone.clear();
    for (auto &l : pictureList) {
        heap_caps_free(l->thumbPixels);
        l->thumbPixels = nullptr;
    }
    for (uint16_t *&pixels : thumbPixels) {
        heap_caps_free(pixels);
        pixels = nullptr;
    }
}

void freeGoneThumbnails() { gone.clear(); }

int current() {
    const auto &s = settings::get();
    if (s.wallpaper != kPictureChoice) return constrain(s.wallpaper, 0, kCount - 1);
    sync();
    for (size_t i = 0; i < pictureList.size(); i++) {
        if (pictureList[i]->picture.id == s.wallpaperPicture) return kCount + i;
    }
    return settings::Settings{}.wallpaper;  // its card is out, or it's gone
}

void choose(int index) {
    auto &s = settings::get();
    if (const Loaded *l = pictureAt(index)) {
        s.wallpaper = kPictureChoice;
        s.wallpaperPicture = l->picture.id;
    } else {
        s.wallpaper = constrain(index, 0, kCount - 1);
    }
    settings::changed();
}

uint32_t revision() {
    sync();
    return rev;
}

std::vector<Picture> pictures() {
    sync();
    std::vector<Picture> out;
    for (const auto &l : pictureList) out.push_back(l->picture);
    return out;
}

uint16_t newPictureId() {
    sync();
    uint16_t id = 1;
    for (const auto &l : pictureList) id = max<uint16_t>(id, l->picture.id + 1);
    return id;
}

String picturePath(uint16_t id, bool thumb) { return String(kDir) + "/" + id + (thumb ? ".thumb" : ".rgb565"); }

void addPicture(uint16_t id, const String &name) {
    sync();
    if (!storage::hasCard()) return;
    auto l = std::make_unique<Loaded>();
    l->picture = {id, name};
    pictureList.push_back(std::move(l));
    saveIndex();
    rev++;
}

void removePicture(uint16_t id) {
    sync();
    for (size_t i = 0; i < pictureList.size(); i++) {
        if (pictureList[i]->picture.id != id) continue;
        retire(std::move(pictureList[i]));
        pictureList.erase(pictureList.begin() + i);
        break;
    }
    if (fullKey == kPictureKey + id) fullKey = -1;
    SD_MMC.remove(picturePath(id, false));
    SD_MMC.remove(picturePath(id, true));
    saveIndex();
    rev++;
}

void renderDesignThumb(int index, uint16_t *out) {
    render(kDesigns[constrain(index, 0, kCount - 1)], kThumbSize, kThumbSize, out);
}

}  // namespace wallpaper
