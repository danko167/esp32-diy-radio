#pragma once

#include <stddef.h>
#include <stdint.h>

// Firmware updates over the USB cable, through the running firmware (like
// OTA, but on the cable), so the screen can show the progress. The laptop
// side is tools/usb_update.py.
namespace usb_update {

// The dev console got "U": asks for "<size>,<md5>\n", then reads the firmware.
// Blocks until done; restarts the radio when it worked. `progress` gets
// 0..100 meanwhile, -1 if it fails.
void receive(void (*progress)(int percent));

// The dev console got "F": asks for "<size>,<path>\n", then writes that file
// to the microSD card (tools/push_file.py sends it)
void receiveFile();
// "K": asks for "<path>\n" and deletes that file from the card
void deleteFile();

// Throws away what comes in on the USB serial until it's been quiet for
// `quietMs`; returns how many bytes went. After a failed transfer, and for the
// dev console on bytes that aren't text: the rest of a firmware chunk, or
// esptool's packets meant for the bootloader, would otherwise be read as
// commands (">" is a volume press, "T" a tap, "t" a chime...).
size_t discardInput(uint32_t quietMs = 300);

}  // namespace usb_update
