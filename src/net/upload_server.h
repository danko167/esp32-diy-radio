#pragma once

#include <Arduino.h>

// A small web server for a computer on the same Wi-Fi: it sends pictures
// for the wallpaper and edits the favourite stations. On only when started
// from the radio (Settings > Display > Your picture), behind a new 4-digit
// code each time, and it stops by itself after a while without activity.
// The page itself is web/upload.html; the browser crops and converts
// pictures, so the radio only stores them.
namespace upload_server {

void start();  // Wi-Fi must be connected
// `why`, if given, shows on the radio's page until the next start
void stop(const char *why = nullptr);
void loop();   // call often, from the UI task

bool running();
const char *stoppedWhy();  // when it stopped by itself for a reason worth showing, until the next start
const char *code();
uint32_t secondsLeft();  // until it stops for lack of activity
String address();        // "http://192.168.1.42"

// A picture on its way
struct Transfer {
    bool active = false;
    String name;
    int percent = 0;
};
Transfer transfer();
void cancelTransfer();  // the radio drops the half-received picture

uint32_t revision();  // changes with any of the above

}  // namespace upload_server
