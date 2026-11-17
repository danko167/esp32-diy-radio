#include "wifi.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <mbedtls/platform.h>

#include <algorithm>

#include "app/watchdog.h"
#include "hal/rtc.h"

// From private/secrets.ini via build_src_flags; empty when there is none
#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif
#ifndef FALLBACK_WIFI_SSID
#define FALLBACK_WIFI_SSID ""
#define FALLBACK_WIFI_PASSWORD ""
#endif

namespace wifi {
namespace {

constexpr char kNamespace[] = "wifi";
constexpr uint32_t kJoinTimeoutMs = 20000;
constexpr uint32_t kTryMs = 15000;          // a saved network gets this long to connect
constexpr uint32_t kFailedSkipMs = 120000;  // then the others go first for this long
constexpr uint32_t kSeekAgainMs = 30000;    // none of them in range: look again after this

// Most recently used first. In NVS as one blob of fixed entries ("nets");
// up to v1 of this file a single "ssid" and "pass", moved over once.
struct Saved {
    String ssid, password;
    uint32_t failedAt = 0;  // millis() of its last failed try, 0 = none
};
std::vector<Saved> saved;
Saved fallback;  // from secrets.ini, used only while nothing is saved; never stored
bool on = true;

struct StoredNet {
    char ssid[33];
    char pass[65];
};

// A join in progress: credentials are only saved once it succeeds
bool joining = false;
String joinSsid, joinPassword;
uint32_t joinStarted = 0;
Status joinResult = Status::NotConfigured;

// A saved network being tried by itself (not a join from the Wi-Fi page)
bool trying = false;
String tryingSsid;
uint32_t tryStarted = 0;
uint32_t nextSeek = 0;  // millis() for the next look for a saved network

// Scans: one at a time, its results kept for the Wi-Fi page
bool scanRunning = false;
bool scanForSeek = false;  // pick a saved network from it when done
std::vector<Network> lastScan;
uint32_t scanCount = 0;      // scans finished
uint32_t scanAskedAt = 0;    // scanCount when the Wi-Fi page asked
bool scanAsked = false;

// Set from the Wi-Fi event task, handled in loop()
volatile bool gotIp = false;
volatile uint8_t disconnectReason = 0;
volatile bool timeSynced = false;
time_t lastSync = 0;

bool otaStarted = false;
void (*updateProgress)(int) = nullptr;

// Each wrong OTA password costs the UI task a PBKDF2 run (10,000 rounds, in
// ArduinoOTA.handle()), so after a few OTA stays off until the next restart
constexpr int kMaxOtaAuthFailures = 5;
int otaAuthFailures = 0;
bool otaLockedOut = false;

void storeNetworks() {
    StoredNet nets[kMaxSaved] = {};
    for (size_t i = 0; i < saved.size() && i < kMaxSaved; i++) {
        strlcpy(nets[i].ssid, saved[i].ssid.c_str(), sizeof(nets[i].ssid));
        strlcpy(nets[i].pass, saved[i].password.c_str(), sizeof(nets[i].pass));
    }
    Preferences prefs;
    prefs.begin(kNamespace, false);
    prefs.putBytes("nets", nets, sizeof(nets));
    prefs.end();
}

void loadNetworks() {
    Preferences prefs;
    prefs.begin(kNamespace, false);
    on = prefs.getBool("on", true);
    saved.clear();
    if (prefs.isKey("nets")) {
        StoredNet nets[kMaxSaved] = {};
        prefs.getBytes("nets", nets, sizeof(nets));
        for (auto &n : nets) {
            n.ssid[sizeof(n.ssid) - 1] = n.pass[sizeof(n.pass) - 1] = '\0';
            if (n.ssid[0]) saved.push_back({n.ssid, n.pass});
        }
        prefs.end();
        return;
    }
    // The one network saved before there could be five
    const String ssid = prefs.getString("ssid", "");
    const String pass = prefs.getString("pass", "");
    prefs.end();
    if (!ssid.isEmpty()) saved.push_back({ssid, pass});
    storeNetworks();
    prefs.begin(kNamespace, false);
    prefs.remove("ssid");
    prefs.remove("pass");
    prefs.end();
}

Saved *findSaved(const String &ssid) {
    for (auto &n : saved) {
        if (n.ssid == ssid) return &n;
    }
    if (saved.empty() && !fallback.ssid.isEmpty() && fallback.ssid == ssid) return &fallback;
    return nullptr;
}

// First in the list, as the most recently used; stored only when that changes something.
// Copies on purpose: callers pass a saved entry's own strings, which erase() moves.
void remember(String ssid, String password) {
    auto it = std::find_if(saved.begin(), saved.end(), [&](const Saved &n) { return n.ssid == ssid; });
    if (it == saved.begin() && it != saved.end() && it->password == password) return;
    if (it != saved.end()) saved.erase(it);
    saved.insert(saved.begin(), {ssid, password});
    if (saved.size() > kMaxSaved) saved.resize(kMaxSaved);
    storeNetworks();
}

void tryNetwork(const Saved &n) {
    trying = true;
    tryingSsid = n.ssid;
    tryStarted = millis();
    disconnectReason = 0;
    WiFi.begin(n.ssid.c_str(), n.password.c_str());
}

void startScanNow(bool forSeek) {
    if (scanRunning) {
        scanForSeek = scanForSeek || forSeek;
        return;
    }
    // The chip refuses to scan in the middle of a connect
    if (trying) {
        trying = false;
        WiFi.disconnect();
    }
    scanForSeek = forSeek;
    scanRunning = WiFi.scanNetworks(true) == WIFI_SCAN_RUNNING;  // async
    if (!scanRunning) nextSeek = millis() + 2000;  // refused: again in a moment
}

// Station mode, as begin() and turning Wi-Fi back on want it
void startStation() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("diy-radio");
    // Not the core's own retrying: seek() picks the saved network each time
    WiFi.setAutoReconnect(false);
    nextSeek = 0;
}

// The strongest saved network in the last scan that hasn't just failed
void pickFromScan() {
    const Saved *best = nullptr;
    int32_t bestRssi = INT32_MIN;
    for (const Network &seen : lastScan) {
        const Saved *n = findSaved(seen.ssid);
        if (!n || (n->failedAt && millis() - n->failedAt < kFailedSkipMs)) continue;
        if (seen.rssi > bestRssi) {
            best = n;
            bestRssi = seen.rssi;
        }
    }
    if (best) {
        Serial.printf("wifi: trying %s (%ld dBm)\n", best->ssid.c_str(), (long)bestRssi);
        tryNetwork(*best);
    } else {
        nextSeek = millis() + kSeekAgainMs;
    }
}

void collectScan() {
    const int16_t count = WiFi.scanComplete();
    if (count == WIFI_SCAN_RUNNING) return;
    scanRunning = false;
    if (count < 0) {  // failed or timed out
        nextSeek = millis() + 2000;
        if (scanAsked) startScanNow(scanForSeek);
        return;
    }
    lastScan.clear();
    for (int i = 0; i < count; i++) {
        const String name = WiFi.SSID(i);
        if (name.isEmpty()) continue;  // hidden networks
        auto existing = std::find_if(lastScan.begin(), lastScan.end(),
                                     [&](const Network &n) { return n.ssid == name; });
        if (existing != lastScan.end()) {
            existing->rssi = std::max(existing->rssi, WiFi.RSSI(i));
            continue;
        }
        lastScan.push_back({name, WiFi.RSSI(i), WiFi.encryptionType(i) != WIFI_AUTH_OPEN});
    }
    std::sort(lastScan.begin(), lastScan.end(),
              [](const Network &a, const Network &b) { return a.rssi > b.rssi; });
    WiFi.scanDelete();
    scanCount++;
    if (scanForSeek && !joining && !trying && !WiFi.isConnected()) pickFromScan();
    scanForSeek = false;
}

// Not connected and not busy: look for a saved network. With just one, try it
// straight away (the chip says quickly when it isn't there); with more, scan
// for the strongest.
void seek() {
    if (!on || joining || trying || scanRunning || WiFi.isConnected()) return;
    if ((int32_t)(millis() - nextSeek) < 0) return;
    const size_t count = saved.empty() ? (fallback.ssid.isEmpty() ? 0 : 1) : saved.size();
    if (count == 0) return;
    if (count == 1) {
        Saved &only = saved.empty() ? fallback : saved[0];
        if (only.failedAt && millis() - only.failedAt < kFailedSkipMs / 4) {
            nextSeek = only.failedAt + kFailedSkipMs / 4;
            return;
        }
        tryNetwork(only);
        return;
    }
    startScanNow(true);
}

void endTry() {
    trying = false;
    if (Saved *n = findSaved(tryingSsid)) n->failedAt = millis() | 1;
    WiFi.disconnect();
    nextSeek = millis() + 1000;
}

void onEvent(arduino_event_id_t event, arduino_event_info_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        gotIp = true;
    } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        disconnectReason = info.wifi_sta_disconnected.reason;
    }
}

bool isAuthFailure(uint8_t reason) {
    return reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
           reason == WIFI_REASON_HANDSHAKE_TIMEOUT || reason == WIFI_REASON_AUTH_EXPIRE;
}

void startOta() {
    if (otaStarted || OTA_PASSWORD[0] == '\0') return;
    otaStarted = true;
    ArduinoOTA.setHostname("diy-radio");
    ArduinoOTA.setPassword(OTA_PASSWORD);
    // The update runs inside ArduinoOTA.handle(), on the loop task: fed with
    // each chunk (it waits up to a second for one)
    ArduinoOTA.onStart([] {
        Serial.println("ota: update started");
        watchdog::feed();
        if (updateProgress) updateProgress(0);
    });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
        watchdog::feed();
        if (updateProgress && total) updateProgress(done * 100 / total);
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("ota: failed, error %u\n", error);
        if (error == OTA_AUTH_ERROR) otaAuthFailures++;  // loop() stops OTA after a few
        if (updateProgress) updateProgress(-1);  // the old firmware carries on
    });
    ArduinoOTA.begin();
    Serial.println("ota: ready at diy-radio.local");
}

// Not from the error callback: that runs inside handle(), which still uses the socket after it
void stopOta() {
    otaLockedOut = true;
    // end() would also stop mDNS, which the upload page uses: only OTA's own entry goes
    ArduinoOTA.setMdnsEnabled(false);
    ArduinoOTA.end();
    MDNS.disableArduino();
    Serial.printf("ota: off after %d wrong passwords, until the next restart\n", otaAuthFailures);
}

void endJoin(Status result) {
    joining = false;
    joinResult = result;
    WiFi.disconnect();
    nextSeek = 0;  // back to a saved network, if one is here
}

}  // namespace

// Encryption (HTTPS streams, TLS) gets its memory from PSRAM. Arduino's build
// keeps it in internal RAM, where a TLS session's ~40 KB doesn't fit next to
// the display buffers and audio. ESP-IDF supports this setup officially.
static void *tlsCalloc(size_t n, size_t size) {
    void *p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

void begin() {
    mbedtls_platform_set_calloc_free(tlsCalloc, heap_caps_free);
    loadNetworks();
    if (FALLBACK_WIFI_SSID[0] != '\0') {
        fallback.ssid = FALLBACK_WIFI_SSID;  // used until a network is saved
        fallback.password = FALLBACK_WIFI_PASSWORD;
    }
    WiFi.persistent(false);  // credentials live in our own NVS namespace
    WiFi.onEvent(onEvent);
    sntp_set_time_sync_notification_cb([](struct timeval *) { timeSynced = true; });
    if (!on) {
        Serial.println("wifi: off");
        return;
    }
    startStation();
}

bool enabled() { return on; }

void setEnabled(bool want) {
    if (want == on) return;
    on = want;
    Preferences prefs;
    prefs.begin(kNamespace, false);
    prefs.putBool("on", on);
    prefs.end();
    joining = trying = false;
    scanAsked = false;
    if (!on) {
        if (scanRunning) WiFi.scanDelete();
        scanRunning = false;
        // OTA's socket goes with the network: started again after the next connect
        // (unless it was locked out after wrong passwords, which lasts till a restart)
        if (otaStarted && !otaLockedOut) {
            ArduinoOTA.end();
            otaStarted = false;
        }
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        Serial.println("wifi: off");
        return;
    }
    for (auto &n : saved) n.failedAt = 0;
    fallback.failedAt = 0;
    startStation();
    Serial.println("wifi: on");
}

void loop() {
    if (gotIp) {
        gotIp = false;
        Serial.printf("wifi: connected to %s, IP %s\n", WiFi.SSID().c_str(),
                      WiFi.localIP().toString().c_str());
        if (joining) {
            joining = false;
            joinResult = Status::Connected;
            remember(joinSsid, joinPassword);
        } else if (trying) {
            trying = false;
            joinResult = Status::Connected;
            if (Saved *n = findSaved(tryingSsid)) {
                n->failedAt = 0;
                if (n != &fallback) remember(n->ssid, n->password);
            }
        }
        configTzTime(rtc::tzString(), "pool.ntp.org", "time.cloudflare.com");
        startOta();
    }
    if (otaStarted && !otaLockedOut) {
        ArduinoOTA.handle();
        if (otaAuthFailures >= kMaxOtaAuthFailures) stopOta();
    }

    if (!on) return;
    if (scanRunning) collectScan();

    if (timeSynced) {
        timeSynced = false;
        rtc::save();
        lastSync = time(nullptr);
        Serial.println("wifi: time synced from NTP");
    }

    if (joining) {
        const uint8_t reason = disconnectReason;
        if (isAuthFailure(reason)) {
            endJoin(Status::WrongPassword);
        } else if (reason == WIFI_REASON_NO_AP_FOUND) {
            endJoin(Status::NotFound);
        } else if (millis() - joinStarted > kJoinTimeoutMs) {
            endJoin(Status::Disconnected);
        }
    } else if (trying) {
        const uint8_t reason = disconnectReason;
        if (isAuthFailure(reason) || reason == WIFI_REASON_NO_AP_FOUND || millis() - tryStarted > kTryMs) {
            Serial.printf("wifi: %s didn't connect (reason %u)\n", tryingSsid.c_str(), reason);
            endTry();
        }
    }
    seek();
}

Status status() {
    if (!on) return Status::Off;
    if (joining) return Status::Connecting;
    if (WiFi.isConnected()) return Status::Connected;
    if (joinResult == Status::WrongPassword || joinResult == Status::NotFound) return joinResult;
    if (saved.empty() && fallback.ssid.isEmpty()) return Status::NotConfigured;
    if (trying) return Status::Connecting;
    return Status::Disconnected;
}

String ssid() {
    if (joining) return joinSsid;
    if (on && WiFi.isConnected()) return WiFi.SSID();
    if (trying) return tryingSsid;
    if (!saved.empty()) return saved[0].ssid;
    return fallback.ssid;
}

String ip() { return WiFi.isConnected() ? WiFi.localIP().toString() : String(); }

time_t lastTimeSync() { return lastSync; }

int32_t rssi() { return WiFi.isConnected() ? WiFi.RSSI() : -127; }

void connect(const String &ssid, const String &password) {
    if (!on) return;
    if (scanRunning) {  // the chip can't do both: stopped, not just its results dropped
        esp_wifi_scan_stop();
        WiFi.scanDelete();
        scanRunning = false;
    }
    trying = false;
    joining = true;
    joinSsid = ssid;
    joinPassword = password;
    joinStarted = millis();
    disconnectReason = 0;
    WiFi.disconnect();
    WiFi.begin(ssid.c_str(), password.c_str());
}

bool connectSaved(const String &ssid) {
    const Saved *n = findSaved(ssid);
    if (!n) return false;
    const String password = n->password;  // connect() may move the list
    connect(ssid, password);
    return true;
}

std::vector<String> savedNetworks() {
    std::vector<String> names;
    for (const auto &n : saved) names.push_back(n.ssid);
    return names;
}

bool isSaved(const String &ssid) {
    return std::any_of(saved.begin(), saved.end(), [&](const Saved &n) { return n.ssid == ssid; });
}

void forget(const String &ssid) {
    auto it = std::find_if(saved.begin(), saved.end(), [&](const Saved &n) { return n.ssid == ssid; });
    if (it == saved.end()) return;
    saved.erase(it);
    storeNetworks();
    // As before: forgetting the last one forgets secrets.ini's too, till a restart
    if (saved.empty()) fallback = Saved{};
    const bool inUse = (on && WiFi.isConnected() && WiFi.SSID() == ssid) || (trying && tryingSsid == ssid) ||
                       (joining && joinSsid == ssid);
    if (inUse) {
        joining = trying = false;
        WiFi.disconnect();
        nextSeek = millis() + 1000;  // another saved one, if it's here
    }
    if (joinResult == Status::WrongPassword || joinResult == Status::NotFound) joinResult = Status::NotConfigured;
}

void onUpdateProgress(void (*progress)(int percent)) { updateProgress = progress; }

void startScan() {
    if (!on) return;
    scanAsked = true;
    scanAskedAt = scanCount;
    if (!joining) startScanNow(!WiFi.isConnected());
}

bool scanResults(std::vector<Network> &out) {
    if (!scanAsked) return false;
    if (!scanRunning && scanCount == scanAskedAt) {  // refused, or a join was in the way: ask again
        if (!joining) startScanNow(!WiFi.isConnected());
        return false;
    }
    if (scanCount == scanAskedAt) return false;
    scanAsked = false;
    out = lastScan;
    return true;
}

}  // namespace wifi
