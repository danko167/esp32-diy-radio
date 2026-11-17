#pragma once

#include <time.h>

// Settings, alarms, favourites and the weather location to /backup on the
// microSD card and back, for after erasing the board or for a second radio.
// The Wi-Fi password stays out: anyone with the card could read it.
namespace backup {

bool save();
time_t lastSaved();  // when the card's backup was made, 0 if it has none

// Puts the card's backup back and restarts the radio, so everything loads it.
// Returns only if it failed.
void restore();

}  // namespace backup
