#pragma once

#include <Arduino.h>
#include <esp_task_wdt.h>

// The loop task (the UI and the alarms) under the task watchdog: stuck for
// 5 s (CONFIG_ESP_TASK_WDT_TIMEOUT_S, with a panic) the radio restarts, so a
// hang never leaves an alarm silent. Arduino feeds it before each loop();
// anything in the loop that can take longer feeds it as it goes.
namespace watchdog {

inline TaskHandle_t watched = nullptr;

// At the end of setup(), which may take longer
inline void begin() {
    enableLoopWDT();
    if (esp_task_wdt_status(nullptr) == ESP_OK) watched = xTaskGetCurrentTaskHandle();  // subscribed
}

// Only on the loop task once it's watched: code shared with other tasks, or
// run from setup(), calls it too
inline void feed() {
    if (watched && xTaskGetCurrentTaskHandle() == watched) esp_task_wdt_reset();
}

}  // namespace watchdog
