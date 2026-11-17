#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <lvgl.h>

#include <vector>

// Clock faces as files on the microSD card, made on the upload page's face
// editor (design/src/faceFile.ts has the format, net/upload_server.cpp the
// endpoints that write them). One folder per face:
//   /faces/<id>/face.json   the FaceFile, format 1, up to kMaxJson bytes
//   /faces/<id>/bg.bin      optional background, 480 x 480 RGB565 like a wallpaper
//   /faces/<id>/<name>.bin  optional fonts, LVGL binary fonts, up to kMaxFont bytes
// The folders are the list: it's read again when a card comes or goes.
// Everything here is for the UI task, except where it says otherwise.
namespace card_faces {

constexpr size_t kMaxJson = 8192;
constexpr size_t kMaxFont = 256 * 1024;
constexpr size_t kBackgroundBytes = 480 * 480 * 2;
constexpr size_t kMaxName = 24;  // characters

// The built-in faces' names, in the order of settings::ClockFace
constexpr const char *kBuiltIn[] = {"Digital", "Analog", "Split", "Night", "Segment"};
constexpr int kBuiltInCount = sizeof(kBuiltIn) / sizeof(kBuiltIn[0]);

struct Face {
    uint16_t id;
    String name;
    uint32_t version;  // new each time it's read, so whoever drew it knows to again
};

uint32_t revision();  // changes when faces come, go or change
std::vector<Face> list();  // by id
const Face *find(uint16_t id);

// The face file, parsed; false when it's missing or not a face
bool load(uint16_t id, JsonDocument &doc);
// After the upload server wrote it: read again, or added
void reload(uint16_t id);
// Deletes its folder; settings using it go back to Digital
void remove(uint16_t id);

// The background into `pixels` (kBackgroundBytes, PSRAM is fine)
bool readBackground(uint16_t id, uint16_t *pixels);
// A font from the face's folder; null when missing or not a font. Free it
// with lv_binfont_destroy().
lv_font_t *loadFont(uint16_t id, const char *file);

// Any task
String folder(uint16_t id);                  // "/faces/3"
String path(uint16_t id, const char *file);  // "/faces/3/face.json"
// bg.bin, or a font: [A-Za-z0-9_-]{1,24}.bin
bool isAssetName(const char *file);
// What a parsed face file must be: format 1, a name of 1..kMaxName characters,
// a background and a list of elements. Says why not in `why`.
bool isFace(JsonVariantConst face, const char *&why);
// The files a face uses: "bg.bin" with a picture, and its fonts
std::vector<String> assets(JsonVariantConst face);

}  // namespace card_faces
