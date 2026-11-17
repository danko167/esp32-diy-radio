#include "rtc.h"

#include <Arduino.h>
#include <RtcDrv.hpp>
#include <Wire.h>
#include <sys/time.h>
#include <time.h>

namespace rtc {
namespace {

// Central European time with DST. Becomes a setting once Wi-Fi/NTP land.
constexpr char kTimezone[] = "CET-1CEST,M3.5.0,M10.5.0/3";
constexpr time_t kYear2025 = 1735689600;

SensorPCF85063 chip;
bool online = false;

// The RTC keeps UTC; mktime() works in local time, so switch TZ briefly
time_t utcToEpoch(struct tm t) {
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t epoch = mktime(&t);
    setenv("TZ", kTimezone, 1);
    tzset();
    return epoch;
}

}  // namespace

bool begin() {
    setenv("TZ", kTimezone, 1);
    tzset();

    online = chip.begin(Wire);
    if (!online) {
        Serial.println("rtc: PCF85063 not found");
        return false;
    }
    if (!chip.isClockIntegrityGuaranteed()) {
        Serial.println("rtc: oscillator was stopped, time not valid");
        return false;
    }

    struct tm t = chip.getDateTime().toUnixTime();
    t.tm_isdst = 0;
    timeval tv{utcToEpoch(t), 0};
    settimeofday(&tv, nullptr);
    return timeValid();
}

bool timeValid() { return time(nullptr) > kYear2025; }

void save() {
    if (!online) return;
    time_t now = time(nullptr);
    struct tm t;
    gmtime_r(&now, &t);
    chip.setDateTime(RTC_DateTime(t));
}

const char *tzString() { return kTimezone; }

}  // namespace rtc
