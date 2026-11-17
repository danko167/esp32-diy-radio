#include "http.h"

#include <HTTPClient.h>

#include "app/version.h"

namespace http {
namespace {

// The biggest real response is radio-browser's 40 stations, all their fields
// before the filter: about 1.5 KB each, so 60 KB and more. The forecast is ~4 KB.
constexpr size_t kMaxBytes = 256 * 1024;
constexpr uint32_t kDeadlineMs = 20000;  // the whole request; each read also times out on its own

// The response as ArduinoJson reads it, ending after kMaxBytes or the
// deadline: a server that never stops, or trickles, can't fill PSRAM or keep
// the task forever. Not a Stream, so ArduinoJson calls these directly.
class LimitedReader {
  public:
    LimitedReader(Stream &in, uint32_t since) : stream(in), started(since) {}

    int read() {
        char c;
        return readBytes(&c, 1) ? (uint8_t)c : -1;
    }

    size_t readBytes(char *buffer, size_t length) {
        if (left == 0 || millis() - started > kDeadlineMs) {
            cut = true;
            return 0;
        }
        const size_t n = stream.readBytes(buffer, min(length, left));
        left -= n;
        return n;
    }

    bool cut = false;

  private:
    Stream &stream;
    const uint32_t started;
    size_t left = kMaxBytes;
};

}  // namespace

bool getJson(const String &url, JsonDocument &doc, const JsonDocument *filter) {
    HTTPClient client;
    client.useHTTP10(true);  // no chunked encoding, so the stream can be parsed directly
    client.setTimeout(10000);
    client.setUserAgent("DIY-Radio/" FIRMWARE_VERSION " (+https://github.com/danko167/esp32-diy-radio)");
    const uint32_t started = millis();  // connecting and the headers count too
    if (!client.begin(url)) return false;
    const int code = client.GET();
    bool ok = false;
    if (code == HTTP_CODE_OK) {
        LimitedReader in(client.getStream(), started);
        const DeserializationError err =
            filter ? deserializeJson(doc, in, DeserializationOption::Filter(*filter))
                   : deserializeJson(doc, in);
        ok = !err;
        if (err) {
            Serial.printf("http: bad JSON from %s: %s%s\n", url.c_str(), err.c_str(),
                          in.cut ? " (cut off: too big or too slow)" : "");
        }
    } else {
        Serial.printf("http: %d from %s\n", code, url.c_str());
    }
    client.end();
    return ok;
}

String urlEncode(const String &s) {
    static const char kHex[] = "0123456789ABCDEF";
    String out;
    for (const char c : s) {
        if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += c;
        } else {
            out += '%';
            out += kHex[(uint8_t)c >> 4];
            out += kHex[(uint8_t)c & 0xF];
        }
    }
    return out;
}

}  // namespace http
