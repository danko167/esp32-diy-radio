#pragma once

namespace rtc {

// Loads the time kept by the onboard PCF85063 into the system clock and
// sets the timezone. Wire must already be started.
bool begin();

// True once the system clock holds a believable time
bool timeValid();

// Store the system clock into the RTC (after an NTP sync or a manual change)
void save();

// POSIX TZ string used for local time
const char *tzString();

}  // namespace rtc
