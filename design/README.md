# Design playground

Sketch the radio's screens in React + CSS and see them in the browser, at the
device's real size (480 × 480). Nothing here runs on the radio: once a screen
looks right, it gets rebuilt in LVGL in `src/ui/`. Changes are tried here
first, and go into the firmware together once they're settled.

```sh
cd design
npm install    # once
npm run dev    # then open the address it prints; edits reload instantly
```

## The picker

The list on the left follows the radio: Home, Internet Radio, Alarms, Weather,
Settings (each page after the one it opens from), the clock screen's faces and
the keyboard, then **On a computer** for the upload page the radio serves to
a browser. A name like `Weather / Next days` puts a screen under its group's
heading, and ` — ` marks a state of a screen (`Weather / Now — no Wi-Fi`).
The address picks the screen, so a link like `#Settings / Display` opens it.

## Files

- `src/screens/`: one file per app, named after its firmware file in its first
  comment, with a stylesheet of the same name (`Weather.tsx`, `weather.css`).
  `ClockFaces.tsx` is the clock screen, `Keyboard.tsx` the text input.
- `src/web/`: the computer's upload page (on the device: `web/upload.html`).
- `src/components.tsx`: the building blocks the firmware has too, same sizes:
  app screen with header, section and section row, card, hint, pill, round
  button, switch, big slider, roller. Each says which firmware helper it is.
- `src/components.css`: their styles, plus `accent`, `dim`, `faint` and
  `small` for text in any screen.
- `src/theme.css`: the firmware's colours and font sizes.
- `src/icons.ts`: every icon available on the device.
- `src/accent.ts`, `src/wallpapers.ts`: the accent colours and the built-in
  wallpapers, as the firmware has them.

## Sizes

Text comes in the sizes the firmware has fonts for (`theme.css`): 20 px for
small print, 22 for body text, 30 for titles and names, 50 for big numbers,
and the clock digits. Another size means another font on the device, so pick
from these. Controls follow the firmware's too: pills are 46, 50 or 58 px
high, header buttons 54.

## What translates well to the device

LVGL styles work much like CSS, so most things carry over directly:

- Flexbox (and grid), padding, gap, margins
- Solid colours, opacity, rounded corners, borders
- The fonts and sizes in `theme.css`, and the icons in `icons.ts`. Another
  size or icon is fine, it only needs adding to `tools/gen_fonts.sh`.
- Scrolling lists, sliders, switches, buttons, animations of position and
  opacity

What costs a lot, or doesn't exist there:

- Blur and `backdrop-filter`, big shadows: very slow on this chip
- CSS transforms (rotate, scale): possible, but slow on large areas
- Gradients: fine as a still background, slow when animated
- Dashed borders: LVGL has none, they become solid
- Web fonts, emoji, SVG: only what's been converted (see above)
- Anything that moves the whole screen every frame (it tops out around 30 fps)
