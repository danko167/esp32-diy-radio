#include "card_faces.h"

#include <SD_MMC.h>
#include <esp_heap_caps.h>

#include <algorithm>

#include "app/psram_json.h"
#include "app/settings.h"
#include "app/storage.h"
#include "app/watchdog.h"

namespace card_faces {
namespace {

constexpr char kDir[] = "/faces";

std::vector<Face> faces;
uint32_t loadedCard = UINT32_MAX;  // storage::cardRevision() the list is from
uint32_t rev = 0;
uint32_t versions = 0;

size_t characters(const char *utf8) {
    size_t n = 0;
    for (; *utf8; utf8++) n += (*utf8 & 0xC0) != 0x80;
    return n;
}

// Reads it as a face, for the list
bool readFace(uint16_t id, Face &out) {
    JsonDocument doc(psram_json::allocator());
    if (!load(id, doc)) return false;
    out = {id, doc["name"].as<const char *>(), ++versions};
    return true;
}

// The list follows the card: read again when one comes or goes
void sync() {
    if (storage::cardRevision() == loadedCard) return;
    loadedCard = storage::cardRevision();
    faces.clear();
    rev++;
    if (!storage::hasCard()) return;
    File dir = SD_MMC.open(kDir);
    if (!dir || !dir.isDirectory()) return;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        const long id = String(f.name()).toInt();
        watchdog::feed();  // a card with many faces
        Face face;
        if (f.isDirectory() && id > 0 && id <= UINT16_MAX && readFace(id, face)) faces.push_back(face);
    }
    std::sort(faces.begin(), faces.end(), [](const Face &a, const Face &b) { return a.id < b.id; });
    Serial.printf("faces: %u on the card\n", faces.size());
}

uint32_t get32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
uint16_t get16(const uint8_t *p) { return p[0] | p[1] << 8; }

// The section at `at` ("head", "cmap"...): its length, 0 if it isn't there
uint32_t section(const uint8_t *data, size_t size, size_t at, const char *tag) {
    if (at + 8 > size || memcmp(data + at + 4, tag, 4) != 0) return 0;
    const uint32_t length = get32(data + at);
    return length >= 8 && length <= size - at ? length : 0;
}

// Where plausibleFont() found the tables, for consistent()
struct Layout {
    size_t locaAt = 0, kernAt = 0;  // kernAt 0: no kerning
    uint32_t glyphs = 0, entry = 0, glyf = 0;
    int32_t headerBytes = 0;  // of each glyph, before its bitmap
};

// LVGL's font loader trusts the counts in the file, and a failed allocation
// stops everything in an assert. So, before it: the sections fit the file,
// and the counts it allocates by fit their sections.
bool plausibleFont(const uint8_t *data, size_t size, Layout &at) {
    const uint32_t head = section(data, size, 0, "head");
    if (head < 8 + 40) return false;
    // font_header_bin_t's tables_count, index_to_loc_format, bits_per_pixel and
    // compression_id: LVGL here draws uncompressed glyphs only
    const uint16_t tables = get16(data + 8 + 4);
    const uint8_t locFormat = data[8 + 26], bpp = data[8 + 29], compression = data[8 + 33];
    if ((bpp != 1 && bpp != 2 && bpp != 4 && bpp != 8) || compression != 0) return false;
    // advance_width_bits + 2 * xy_bits + 2 * wh_bits, as the loader skips them
    at.headerBytes = (data[8 + 32] + 2 * data[8 + 30] + 2 * data[8 + 31]) / 8;
    const size_t cmapAt = head;
    const uint32_t cmap = section(data, size, cmapAt, "cmap");
    if (cmap < 12) return false;
    const uint32_t subtables = get32(data + cmapAt + 8);
    if (subtables > (cmap - 12) / 16) return false;
    for (uint32_t i = 0; i < subtables; i++) {
        const uint8_t *t = data + cmapAt + 12 + i * 16;
        if (get16(t + 12) > cmap / 2) return false;  // data_entries_count
        // A full format 0 range is looked up in data_entries_count offsets
        if (t[14] == LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL && get16(t + 8) > get16(t + 12)) return false;
    }
    const size_t locaAt = cmapAt + cmap;
    const uint32_t loca = section(data, size, locaAt, "loca");
    if (loca < 12 || locFormat > 1) return false;
    const uint32_t entry = locFormat ? 4 : 2;
    const uint32_t count = get32(data + locaAt + 8);
    if (count == 0 || count > (loca - 12) / entry) return false;
    const size_t glyfAt = locaAt + loca;
    const uint32_t glyf = section(data, size, glyfAt, "glyf");
    if (!glyf) return false;
    // The bitmaps add up from the offsets, so they mustn't go back or past the end
    uint32_t last = 0;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *p = data + locaAt + 12 + i * entry;
        const uint32_t offset = locFormat ? get32(p) : get16(p);
        if (offset < last || offset > glyf) return false;
        last = offset;
    }
    at.locaAt = locaAt;
    at.glyphs = count;
    at.entry = entry;
    at.glyf = glyf;
    if (tables < 4) return true;  // no kerning
    const size_t kernAt = glyfAt + glyf;
    at.kernAt = kernAt;
    const uint32_t kern = section(data, size, kernAt, "kern");
    if (kern < 16) return false;
    // Sorted pairs: up to 2 ids and a value each, by a count; classes are small
    return data[kernAt + 8] != 0 || get32(data + kernAt + 12) <= kern / 3;
}

// Drawing trusts the loaded font in turn: it indexes the glyphs, bitmaps and
// kerning classes with what the file says, unchecked. So, after the loader:
// every glyph a character leads to is in the font, every glyph's bitmap is
// within the bitmaps, and every kerning class within its table.
bool consistent(const lv_font_t *font, const uint8_t *data, const Layout &at) {
    const auto *dsc = static_cast<const lv_font_fmt_txt_dsc_t *>(font->dsc);
    uint32_t highest = 0;  // glyph id
    for (uint16_t i = 0; i < dsc->cmap_num; i++) {
        const lv_font_fmt_txt_cmap_t &c = dsc->cmaps[i];
        const bool sparse = c.type == LV_FONT_FMT_TXT_CMAP_SPARSE_FULL || c.type == LV_FONT_FMT_TXT_CMAP_SPARSE_TINY;
        const uint32_t n = sparse ? c.list_length : c.range_length;
        if (!n) continue;
        uint32_t most = n - 1;  // the tiny ones count up from glyph_id_start
        if (c.type == LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL) {
            const auto *ofs = static_cast<const uint8_t *>(c.glyph_id_ofs_list);
            most = *std::max_element(ofs, ofs + n);
        } else if (c.type == LV_FONT_FMT_TXT_CMAP_SPARSE_FULL) {
            const auto *ofs = static_cast<const uint16_t *>(c.glyph_id_ofs_list);
            most = *std::max_element(ofs, ofs + n);
        }
        highest = LV_MAX(highest, c.glyph_id_start + most);
    }
    if (highest >= at.glyphs) return false;

    // The bitmaps' size, added up from the offsets as the loader does
    const uint8_t *loca = data + at.locaAt + 12;
    auto offset = [&](uint32_t i) -> uint32_t {
        if (i == at.glyphs) return at.glyf;
        return at.entry == 4 ? get32(loca + i * 4) : get16(loca + i * 2);
    };
    uint32_t bitmaps = 0;
    for (uint32_t i = 1; i < at.glyphs; i++) {
        if (dsc->glyph_dsc[i].box_w * dsc->glyph_dsc[i].box_h == 0) continue;
        const int32_t bytes = (int32_t)(offset(i + 1) - offset(i)) - at.headerBytes;
        if (bytes <= 0) return false;
        bitmaps += bytes;
    }
    for (uint32_t i = 1; i < at.glyphs; i++) {
        const lv_font_fmt_txt_glyph_dsc_t &g = dsc->glyph_dsc[i];
        const uint32_t bits = (uint32_t)g.box_w * g.box_h * dsc->bpp;
        if (g.bitmap_index + (bits + 7) / 8 > bitmaps) return false;
    }

    // Kerning classes are looked up by glyph id, then in a rows x columns table
    if (dsc->kern_dsc && dsc->kern_classes) {
        const auto *k = static_cast<const lv_font_fmt_txt_kern_classes_t *>(dsc->kern_dsc);
        const uint16_t mapped = get16(data + at.kernAt + 12);  // kern_class_mapping_length
        if (mapped <= highest) return false;
        for (uint16_t i = 0; i < mapped; i++) {
            if (k->left_class_mapping[i] > k->left_class_cnt || k->right_class_mapping[i] > k->right_class_cnt) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

uint32_t revision() {
    sync();
    return rev;
}

std::vector<Face> list() {
    sync();
    return faces;
}

const Face *find(uint16_t id) {
    sync();
    for (const Face &f : faces) {
        if (f.id == id) return &f;
    }
    return nullptr;
}

bool load(uint16_t id, JsonDocument &doc) {
    if (!storage::hasCard()) return false;
    File f = SD_MMC.open(path(id, "face.json"), "r");
    if (!f || f.size() > kMaxJson) return false;
    const char *why;
    return psram_json::parse(doc, f) && isFace(doc.as<JsonVariantConst>(), why);
}

void reload(uint16_t id) {
    sync();
    auto at = std::find_if(faces.begin(), faces.end(), [id](const Face &f) { return f.id == id; });
    Face face;
    if (!readFace(id, face)) {
        if (at != faces.end()) faces.erase(at);
    } else if (at != faces.end()) {
        *at = face;
    } else {
        faces.insert(std::upper_bound(faces.begin(), faces.end(), face,
                                      [](const Face &a, const Face &b) { return a.id < b.id; }),
                     face);
    }
    rev++;
}

void remove(uint16_t id) {
    sync();
    auto &s = settings::get();
    if (s.clockFace == settings::ClockFace::Card && s.clockFaceFile == id) {
        s.clockFace = settings::ClockFace::Digital;
        settings::changed();
    }
    faces.erase(std::remove_if(faces.begin(), faces.end(), [id](const Face &f) { return f.id == id; }), faces.end());
    rev++;
    if (!storage::hasCard()) return;
    std::vector<String> files;
    File dir = SD_MMC.open(folder(id));
    if (dir && dir.isDirectory()) {
        for (File f = dir.openNextFile(); f; f = dir.openNextFile()) files.push_back(f.name());
    }
    dir.close();
    for (const String &name : files) SD_MMC.remove(folder(id) + "/" + name);
    SD_MMC.rmdir(folder(id));
}

bool readBackground(uint16_t id, uint16_t *pixels) {
    if (!storage::hasCard()) return false;
    watchdog::feed();  // 450 KB
    File f = SD_MMC.open(path(id, "bg.bin"), "r");
    return f && f.size() == kBackgroundBytes &&
           f.read(reinterpret_cast<uint8_t *>(pixels), kBackgroundBytes) == kBackgroundBytes;
}

lv_font_t *loadFont(uint16_t id, const char *file) {
    if (!storage::hasCard() || !isAssetName(file) || !strcmp(file, "bg.bin")) return nullptr;
    watchdog::feed();  // fonts are up to 256 KB
    File f = SD_MMC.open(path(id, file), "r");
    if (!f || f.size() < 64 || f.size() > kMaxFont) return nullptr;
    const size_t size = f.size();
    auto *data = static_cast<uint8_t *>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM));
    if (!data) return nullptr;
    lv_font_t *font = nullptr;
    Layout at;
    // The loader copies what it needs, so the file's bytes go right after
    if (f.read(data, size) == size && plausibleFont(data, size, at)) {
        font = lv_binfont_create_from_buffer(data, size);
        if (font && !consistent(font, data, at)) {
            lv_binfont_destroy(font);
            font = nullptr;
        }
    }
    heap_caps_free(data);
    if (!font) Serial.printf("faces: %s of face %u isn't a font\n", file, id);
    return font;
}

String folder(uint16_t id) { return String(kDir) + "/" + id; }

String path(uint16_t id, const char *file) { return folder(id) + "/" + file; }

bool isAssetName(const char *file) {
    const size_t n = strlen(file);
    if (n < 5 || n > 24 + 4 || strcmp(file + n - 4, ".bin") != 0) return false;
    for (size_t i = 0; i < n - 4; i++) {
        if (!isalnum((unsigned char)file[i]) && file[i] != '_' && file[i] != '-') return false;
    }
    return true;
}

bool isFace(JsonVariantConst face, const char *&why) {
    why = nullptr;
    const char *name = face["name"] | "";
    if (face["format"] != 1) {
        why = "Not a format 1 face";
    } else if (!name[0] || characters(name) > kMaxName) {
        why = "The name needs 1 to 24 characters";
    } else if (!face["background"].is<JsonObjectConst>()) {
        why = "No background";
    } else if (!face["elements"].is<JsonArrayConst>()) {
        why = "No elements";
    }
    return !why;
}

std::vector<String> assets(JsonVariantConst face) {
    std::vector<String> out;
    if (face["background"]["picture"].is<const char *>()) out.push_back("bg.bin");
    for (JsonObjectConst e : face["elements"].as<JsonArrayConst>()) {
        const char *file = e["font"]["file"] | "";
        if (isAssetName(file) && std::find(out.begin(), out.end(), file) == out.end()) out.push_back(file);
    }
    return out;
}

}  // namespace card_faces
