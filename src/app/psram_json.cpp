#include "psram_json.h"

#include <esp_heap_caps.h>

namespace psram_json {
namespace {

struct PsramAllocator : ArduinoJson::Allocator {
    void *allocate(size_t size) override { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM); }
    void deallocate(void *p) override { heap_caps_free(p); }
    void *reallocate(void *p, size_t size) override {
        return heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM);
    }
};

}  // namespace

ArduinoJson::Allocator *allocator() {
    static PsramAllocator instance;
    return &instance;
}

void serialize(const JsonDocument &doc, String &out) { serializeJson(doc, out); }
void serialize(const JsonDocument &doc, Print &out) { serializeJson(doc, out); }
bool parse(JsonDocument &doc, const String &text) { return !deserializeJson(doc, text); }
bool parse(JsonDocument &doc, Stream &in) { return !deserializeJson(doc, in); }

}  // namespace psram_json
