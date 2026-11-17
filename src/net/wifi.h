#pragma once

#include <Arduino.h>

#include <vector>

// Station-mode Wi-Fi with up to 5 saved networks, like a phone: when it isn't
// connected, it joins the strongest saved one in range. Plus NTP time sync
// once online. Everything here is non-blocking; call loop() from the main loop.
namespace wifi {

enum class Status { NotConfigured, Connecting, Connected, Disconnected, WrongPassword, NotFound, Off };

constexpr int kMaxSaved = 5;

struct Network {
    String ssid;
    int32_t rssi;
    bool secured;
};

void begin();  // connects to a saved network, if any
void loop();

// Off: the radio part shut down (its memory free), remembered across restarts.
// The saved networks stay; on again, it joins one.
bool enabled();
void setEnabled(bool on);

Status status();
String ssid();  // network being used, joined or tried, else the last used; empty if none
String ip();
int32_t rssi();
time_t lastTimeSync();  // when the clock was last set from the internet, 0 = never

// Join a network; the credentials are saved once it connects (first in the
// list, the one used longest ago dropped for a sixth)
void connect(const String &ssid, const String &password);
bool connectSaved(const String &ssid);  // with its saved password; false if it isn't saved
std::vector<String> savedNetworks();  // most recently used first
bool isSaved(const String &ssid);
void forget(const String &ssid);

// Firmware updates over Wi-Fi (when built with an OTA password). An update
// blocks the main loop until it's done; `progress` gets 0..100 meanwhile, -1 if it fails.
void onUpdateProgress(void (*progress)(int percent));

// Asynchronous scan: start, then poll scanResults() until it returns true
// (it also takes a scan of its own made since). Results are sorted by signal
// strength, one entry per network name.
void startScan();
bool scanResults(std::vector<Network> &out);

}  // namespace wifi
