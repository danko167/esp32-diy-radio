#pragma once

#include <ArduinoJson.h>

namespace psram_json {

// For JsonDocument: keeps parsed JSON in PSRAM, away from the small internal
// RAM that Wi-Fi needs. Use as JsonDocument doc(psram_json::allocator());
ArduinoJson::Allocator *allocator();

// ArduinoJson's writer and parser, compiled once here: each file that calls
// serializeJson() or deserializeJson() itself gets its own copy of them
// (2 KB and more each, which the linker can't merge)
void serialize(const JsonDocument &doc, String &out);
void serialize(const JsonDocument &doc, Print &out);  // a file
bool parse(JsonDocument &doc, const String &text);  // false when it isn't JSON
bool parse(JsonDocument &doc, Stream &in);         // a file

}  // namespace psram_json
