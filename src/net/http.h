#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Small blocking HTTP helpers, for background tasks only (never the UI).
namespace http {

// GET a URL and parse the JSON response. `filter` keeps only the fields
// that matter, so big responses stay small in memory.
bool getJson(const String &url, JsonDocument &doc, const JsonDocument *filter = nullptr);

String urlEncode(const String &s);

}  // namespace http
