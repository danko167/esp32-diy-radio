#include <Arduino.h>
#include <SD_MMC.h>
#include <Wire.h>
#include <lvgl.h>

#include "app/alarms.h"
#include "app/radio.h"
#include "app/settings.h"
#include "app/storage.h"
#include "app/usb_update.h"
#include "app/watchdog.h"
#include "audio/player.h"
#include "audio/recorder.h"
#include "audio/tones.h"
#include "hal/buttons.h"
#include "hal/display.h"
#include "hal/i2c_scan.h"
#include "hal/imu.h"
#include "hal/pins.h"
#include "hal/power.h"
#include "hal/rtc.h"
#include "net/stations.h"
#include "net/weather.h"
#include "net/upload_server.h"
#include "net/wifi.h"
#include "ui/ui.h"

// The loop task runs LVGL. With Arduino's 8 KB, drawing the deepest screens
// left under 500 bytes unused, and an LVGL warning printed from inside a draw
// overflowed it (seen as a "Double exception" restart). The dev console's h
// shows what's never been used.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#include "ui/wallpaper.h"

void setup() {
    Serial.begin(115200);
    // Never wait on the USB log: with the board plugged in but no serial
    // monitor reading, every line would stall the UI (and the buttons) 100 ms
    Serial.setTxTimeoutMs(0);
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

    settings::load();
    if (!display::begin()) {
        Serial.println("Display bring-up failed");
        return;
    }
    if (!rtc::begin()) Serial.println("Clock not set");
    power::begin();
    imu::begin();

    storage::begin();
    tones::begin();
    if (player::begin()) player::setVolume(settings::get().volume);
    stations::begin();
    alarms::begin();

    wifi::begin();
    weather::begin();
    ui::begin();
    wifi::onUpdateProgress(ui::showUpdateProgress);
    Serial.printf("DIY Radio: setup done, %lu KB RAM / %lu KB PSRAM free\n",
                  ESP.getFreeHeap() / 1024, ESP.getFreePsram() / 1024);
    // Only the idle task of core 0 is watched by default; this is the one that rings the alarms
    watchdog::begin();
}

// Time one full redraw of the active screen
static void timeFullRedraw() {
    lv_obj_invalidate(lv_screen_active());
    const uint32_t start = micros();
    lv_refr_now(nullptr);
    Serial.printf("full redraw: %lu us\n", micros() - start);
}

// Serial commands, for testing without touching the screen:
// s = Settings, w = Weather, i = Internet radio, m = Alarms, b = back (l = back, leaving left), z = ambient (sleep), a = wake,
// r = time a full redraw, q = UI state, p = next wallpaper, u = test stream, t = chime, x = stop audio,
// h = memory and battery, f = list favourites and play the first,
// v = screenshot (tools/screenshot.py saves it as a PNG), T<x>,<y> = tap there (H<x>,<y>: hold),
// D<x1>,<y1>,<x2>,<y2> = drag a finger from one point to the other in 250 ms (swipes, scrolls),
// U = firmware update over the cable (tools/usb_update.py does it), F = a file to the
// microSD card (tools/push_file.py), K = delete one there (push_file.py --delete), W = upload server on/off,
// S<dy> = scroll what's under the middle of the screen by dy pixels (positive: further down), A = add an alarm a minute from now,
// < > ^ = press the left, right or middle button, 0-9 = hide/show that child of the screen,
// R = record Voice, or stop (M: record Music), g = the motion sensor's readings (its own axes and the screen's),
// I = I2C scan at 100 kHz (an MCP23017 found gets a few of its registers read),
// B<mV> = pretend the battery reads that (2500..4500), on battery without USB: B3400 is low 10 s later, B3200
// empty (the screen and the saving, but it stays on while pretending), B0 = the real battery again

// The active screen's pixels as raw RGB565 between two text markers. A full
// screen panel on the top layer (the ringing alarm) is taken instead.
static void sendScreenshot() {
    lv_obj_t *what = lv_screen_active();
    for (uint32_t i = 0; i < lv_obj_get_child_count(lv_layer_top()); i++) {
        lv_obj_t *child = lv_obj_get_child(lv_layer_top(), i);
        if (!lv_obj_is_hidden(child) && lv_obj_get_width(child) == LCD_WIDTH &&
            lv_obj_get_height(child) == LCD_HEIGHT) {
            what = child;
        }
    }
    lv_draw_buf_t *shot = lv_snapshot_take(what, LV_COLOR_FORMAT_RGB565);
    if (!shot) {
        Serial.println("screenshot: out of memory");
        return;
    }
    Serial.printf("\nSCREENSHOT %u %u\n", shot->header.w, shot->header.h);
    const uint8_t *p = shot->data;
    size_t left = shot->header.stride * shot->header.h;
    // In chunks, so the USB serial buffer keeps up; given up when nothing reads it
    for (uint32_t moved = millis(); left && millis() - moved < 2000;) {
        const size_t n = Serial.write(p, min<size_t>(left, 4096));
        if (n) moved = millis();
        p += n;
        left -= n;
        watchdog::feed();
    }
    Serial.println("\nEND SCREENSHOT");
    lv_draw_buf_destroy(shot);
}

static void devConsole() {
    while (Serial.available()) {
        watchdog::feed();  // a script's commands can come many at a time, parseInt() waits up to 1 s each
        const int c = Serial.read();
        // Our scripts send text only. Anything else is binary that wasn't meant
        // for the console (a failed upload's rest, esptool's packets), and its
        // bytes would run as commands: all of it goes.
        if (c >= 0x80 || (c < 0x20 && c != '\n' && c != '\r')) {
            const size_t dropped = usb_update::discardInput() + 1;
            Serial.printf("console: dropped %u bytes that weren't text\n", (unsigned)dropped);
            continue;
        }
        switch (c) {
            case 's': ui::open(ui::App::Settings); break;
            case 'w': ui::open(ui::App::Weather); break;
            case 'i': ui::open(ui::App::Internet); break;
            case 'm': ui::open(ui::App::Alarm); break;
            case 'b': ui::back(); break;
            case 'l': ui::back(ui::Exit::ToLeft); break;
            case 'z': ui::sleep(); break;
            case 'a': ui::wake(); break;
            case 'r': timeFullRedraw(); break;
            case 'v': sendScreenshot(); break;
            case 'q': ui::printState(); break;
            case '<': buttons::simulate(buttons::Key::Left, buttons::Gesture::Press); break;
            case '>': buttons::simulate(buttons::Key::Right, buttons::Gesture::Press); break;
            case '^': buttons::simulate(buttons::Key::Middle, buttons::Gesture::Press); break;
            case 'T':
            case 'H': {
                const int x = Serial.parseInt(), y = Serial.parseInt();
                display::simulateTouch(x, y, c == 'H' ? 900 : 80);
                break;
            }
            case 'D': {
                const int x1 = Serial.parseInt(), y1 = Serial.parseInt();
                const int x2 = Serial.parseInt(), y2 = Serial.parseInt();
                display::simulateDrag(x1, y1, x2, y2);
                break;
            }
            case 'S': {
                const int dy = Serial.parseInt();
                lv_point_t middle = {LCD_WIDTH / 2, LCD_HEIGHT / 2};
                lv_obj_t *obj = lv_indev_search_obj(lv_screen_active(), &middle);
                while (obj && !(lv_obj_check_type(obj, &lv_obj_class) &&  // not a roller or such
                                lv_obj_is_scrollable(obj) &&
                                lv_obj_get_scroll_dir(obj) & LV_DIR_VER &&
                                lv_obj_get_scroll_top(obj) + lv_obj_get_scroll_bottom(obj) > 0)) {
                    obj = lv_obj_get_parent(obj);
                }
                if (obj) lv_obj_scroll_by_bounded(obj, 0, -dy, LV_ANIM_OFF);
                lv_display_trigger_activity(lv_display_get_default());
                break;
            }
            case 'U': usb_update::receive(ui::showUpdateProgress); break;
            case 'F': usb_update::receiveFile(); break;
            case 'K': usb_update::deleteFile(); break;
            case 'W':  // the upload server on or off, as from Settings > Display > Your picture
                if (upload_server::running()) {
                    upload_server::stop();
                } else {
                    upload_server::start();
                }
                break;
            case 'R':
            case 'M':
                if (recorder::recording()) {
                    recorder::stop();
                } else if (!recorder::start(c == 'M' ? recorder::Quality::Music : recorder::Quality::Voice)) {
                    Serial.printf("recorder: %s\n", recorder::why() ? recorder::why() : "busy");
                }
                break;
            case 'A': {
                const time_t at = time(nullptr) + 60;
                struct tm t;
                localtime_r(&at, &t);
                alarms::Alarm a;
                a.hour = t.tm_hour;
                a.minute = t.tm_min;
                a.label = "Test";
                alarms::save(a);
                Serial.printf("alarm at %02d:%02d\n", a.hour, a.minute);
                break;
            }
            case 'I': i2c_scan::run(); break;
            case 'g': {  // started for the moment when the Level isn't open
                const bool wasOn = imu::running();
                imu::start();
                for (int i = 0; i < 8; i++) {
                    delay(i ? 100 : 300);
                    float cx, cy, cz, x, y, z;
                    if (imu::readChip(cx, cy, cz) && imu::read(x, y, z)) {
                        Serial.printf("imu: chip x %.3f y %.3f z %.3f g; screen x %.3f y %.3f z %.3f g\n", cx, cy,
                                      cz, x, y, z);
                    } else {
                        Serial.println(imu::present() ? "imu: no reading" : "imu: not found");
                    }
                }
                if (!wasOn) imu::stop();
                break;
            }
            case 'u': player::playUrl("http://stream.radioparadise.com/mp3-128", "Radio Paradise"); break;
            case 't': tones::play("tone:chime"); break;
            case 'x': player::stop(); break;
            case 'f': {  // list favourites, play the first
                const auto favs = stations::favourites();
                for (const auto &s : favs) Serial.printf("fav: %s  %s\n", s.name.c_str(), s.url.c_str());
                if (!favs.empty()) player::playUrl(favs[0].url.c_str(), favs[0].name.c_str());
                break;
            }
            case 'h':
                Serial.printf("heap: internal %u KB free (lowest %u KB, largest block %u KB), PSRAM %u KB free\n",
                              heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024,
                              heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024,
                              heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024,
                              heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
                Serial.printf("cpu: %lu MHz, loop task stack: %u bytes never used\n", getCpuFrequencyMhz(),
                              uxTaskGetStackHighWaterMark(nullptr));
                for (const char *task : {"player", "weather", "stations", "rec", "rec-in"}) {
                    TaskHandle_t handle = xTaskGetHandle(task);
                    if (handle) Serial.printf("%s task stack: %u bytes never used\n", task, uxTaskGetStackHighWaterMark(handle));
                }
                {
                    const power::Battery &b = power::battery();
                    static const char *const kLevels[] = {"ok", "low", "critical"};
                    Serial.printf("battery: %s, %d%% (gauge %d%%, full at %d%%), %u mV, %s%s%s%s\n",
                                  b.present ? "present" : "absent", b.percent, b.gauge, power::fullAt(),
                                  b.millivolts, kLevels[static_cast<int>(b.level)],
                                  b.charging ? ", charging" : "", b.usb ? ", USB in" : "",
                                  power::pretending() ? " (pretend)" : "");
                    const power::Readings &r = power::readings();
                    Serial.printf("power chip: USB %u mV, %.1f C\n", r.usbMv, r.chipC);
                }
                if (storage::hasCard()) {
                    Serial.printf("card: %llu MB, %llu MB free\n", SD_MMC.cardSize() / (1024 * 1024),
                                  (SD_MMC.totalBytes() - SD_MMC.usedBytes()) / (1024 * 1024));
                } else {
                    Serial.println("card: none");
                }
                {
                    const power::ChargeSettings c = power::chargeSettings();
                    Serial.printf("charging: to %u mV at %u mA, stops at %u mA (%s), off below %u mV, "
                                  "USB limit %u mA\n",
                                  c.targetMv, c.currentMa, c.stopMa, c.stopOn ? "on" : "off", c.cutoffMv,
                                  c.usbLimitMa);
                }
                break;
            case 'B': {
                const long mv = Serial.parseInt();
                if (mv != 0 && (mv < 2500 || mv > 4500)) {
                    Serial.printf("battery: %ld mV? 2500..4500, or B0 for the real one\n", mv);
                    break;
                }
                power::pretend(mv);
                if (mv) {
                    Serial.printf("battery: pretending %ld mV, no USB\n", mv);
                } else {
                    Serial.println("battery: real again");
                }
                break;
            }
            case 'p':
                wallpaper::choose((wallpaper::current() + 1) % wallpaper::count());
                ui::applySettings();
                break;
            case '0' ... '9': {  // hide/show a child of the screen, to find what's slow
                lv_obj_t *child = lv_obj_get_child(lv_screen_active(), c - '0');
                if (child) lv_obj_set_hidden(child, !lv_obj_is_hidden(child));
                break;
            }
        }
    }
}

void loop() {
    const uint32_t idleMs = lv_timer_handler();
    wifi::loop();
    weather::loop();
    buttons::loop();
    power::loop();
    alarms::loop();
    radio::loop();
    settings::loop();
    storage::loop();
    upload_server::loop();
    recorder::loop();
    devConsole();
    delay(idleMs < 5 ? idleMs : 5);
}
