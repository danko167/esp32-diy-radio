#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include <vector>

// Wallpapers: designs generated on the device from a few soft colour blobs
// (no image files, any size), then pictures sent from a computer to the
// microSD card (net/upload_server). Indexes count the designs first.
// Everything here is for the UI task, except where it says otherwise.
namespace wallpaper {

int count();
int designCount();
const char *name(int index);
bool isPicture(int index);

// Full-screen image, or nullptr for plain black. Rendered or loaded on first
// use; only the most recent one is kept (460 KB in PSRAM).
const lv_image_dsc_t *image(int index);

// Small preview for the settings picker (87 KB in PSRAM each). It stays put
// while shown, even when its picture goes, until one of these says nothing
// shows it any more: all of them (the picker closed), or just those of
// pictures gone (the picker emptied to be filled again).
const lv_image_dsc_t *thumbnail(int index);
constexpr int kThumbSize = 208;  // two across the settings page
void freeThumbnails();
void freeGoneThumbnails();

// The one in use, from settings. A picture whose card is out shows a design
// meanwhile, and comes back with the card.
int current();
void choose(int index);

// Pictures: a 480 x 480 RGB565 file and a thumbnail on the card, plus an index
uint32_t revision();  // changes when pictures come or go
struct Picture {
    uint16_t id;
    String name;
};
std::vector<Picture> pictures();
uint16_t newPictureId();
String picturePath(uint16_t id, bool thumb);  // any task
constexpr size_t kPictureBytes = 480 * 480 * 2;
constexpr size_t kThumbBytes = kThumbSize * kThumbSize * 2;
void addPicture(uint16_t id, const String &name);  // once its files are written
void removePicture(uint16_t id);                   // deletes its files

// A design's thumbnail into `out` (kThumbSize² pixels), any task
void renderDesignThumb(int index, uint16_t *out);

}  // namespace wallpaper
