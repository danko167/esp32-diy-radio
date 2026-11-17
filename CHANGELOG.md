# Changelog

All notable changes to this project are listed here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] – 2026-11-17

### Added

- Auto-rotate: a button in the control centre. Stand the radio on its left or
  right side and the screen turns to match; off by default, and the motion
  sensor only runs while it's on.

## [0.1.0] – 2026-11-17

First public release.

### Features

- Internet radio from the radio-browser.info directory: popular stations in
  your country, recently played, search, favourites as numbered presets, a
  sleep timer and a list of recently heard songs.
- Up to 20 alarms, one-off or on chosen days, each with its own sound (built-in
  tones, files from the microSD card or a favourite station), volume, fade-in
  and snooze.
- Clock set from the internet and kept by the real-time clock, or set by hand.
- Clock screen with Digital, Analog, Split, Night and Segment faces, optional
  weather, its own brightness, and a screen lock.
- Weather from Open-Meteo: now, the next 24 hours, the next days and details,
  for a location found automatically or searched by name.
- Recorder: Voice or Music WAV recordings from the two microphones to the card.
- Level: a spirit level using the motion sensor.
- Personalising: built-in and your own wallpapers, eight accent colours, clock
  faces designed in the browser and stored on the card, and the Home tile order.
- Control centre (swipe down): Wi-Fi, Radio, Upload and Lock toggles,
  brightness and volume. Up to five saved Wi-Fi networks.
- Upload page served to a browser on the same Wi-Fi, protected by a 4-digit
  code: wallpapers, a clock face editor, recordings and favourite stations.
- Battery support: gentle charging, a low-battery notice and a clean
  switch-off when empty.
- Reliability: a watchdog, alarms that fall back to a built-in chime when they
  make no sound, a microSD card that can come and go while the radio runs, and
  backups of settings, alarms and favourites to the card.
- Updates over the USB cable (with progress on screen) or over Wi-Fi (OTA).
- FM Radio and Bluetooth tiles are placeholders for now.

[0.2.0]: https://github.com/danko167/esp32-diy-radio/releases/tag/v0.2.0
[0.1.0]: https://github.com/danko167/esp32-diy-radio/releases/tag/v0.1.0
