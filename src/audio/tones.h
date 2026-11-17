#pragma once

#include <Arduino.h>

#include <vector>

// Alarm sounds: a few tones generated on the device (so no audio files ship
// with the code), plus any MP3/WAV in a "sounds" folder on the microSD card.
namespace tones {

struct Sound {
    String id;    // what an alarm stores: "tone:chime", "sd:/sounds/x.mp3", "radio:<url>"
    String name;  // shown to the user
};

void begin();  // writes missing tone files; storage must be mounted

std::vector<Sound> list();  // built-in tones, then the card's sounds

// Starts a sound by id, repeating for files. False if it can't be played.
bool play(const String &id);

}  // namespace tones
