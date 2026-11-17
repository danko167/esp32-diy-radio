#pragma once

#include <Arduino.h>
#include <time.h>

#include <vector>

// Alarms: one-off or repeating on chosen days, each with its own sound.
// Kept in internal flash; loop() rings them when due.
namespace alarms {

// Day bits: bit 0 = Monday ... bit 6 = Sunday. No bits = rings once.
constexpr uint8_t kWeekdays = 0x1F;
constexpr uint8_t kWeekend = 0x60;
constexpr uint8_t kEveryDay = 0x7F;

struct Alarm {
    uint32_t id = 0;  // 0 = not saved yet
    bool enabled = true;
    uint8_t hour = 7;
    uint8_t minute = 0;
    uint8_t days = 0;
    String label;
    String sound = "tone:chime";  // see tones::Sound::id
    String soundName = "Chime";
    uint8_t volume = 60;
    bool gentle = true;  // fade in over a minute instead of starting loud
    uint8_t snoozeMinutes = 9;
};

void begin();  // storage must be mounted; picks up a ring or snooze a restart cut short
void loop();   // call often; restarts the radio (once) if no alarm sound comes out

std::vector<Alarm> list();  // sorted by time of day
uint32_t revision();        // changes whenever the list or ringing state changes
void save(Alarm a);         // adds when id is 0, otherwise replaces
void remove(uint32_t id);
void setEnabled(uint32_t id, bool on);

// Next time this alarm rings after `now`; 0 when it's off
time_t nextRing(const Alarm &a, time_t now);
// Earliest upcoming ring of any alarm, 0 when none
time_t nextRing(time_t now);

// "Once", "Every day", "Weekdays", "Mon, Wed, Fri"
String describeDays(uint8_t days);

// Ringing
bool ringing(Alarm *which = nullptr);
void snooze();
void dismiss();
time_t snoozedUntil();  // 0 when nothing is snoozed
void cancelSnooze();

}  // namespace alarms
