#!/usr/bin/env bash
# Regenerates the LVGL fonts in src/ui/fonts from tools/fonts.
# Needs Node.js (runs lv_font_conv through npx).
set -euo pipefail

cd "$(dirname "$0")/fonts"
OUT=../../src/ui/fonts
# Uncompressed: unpacking glyphs at draw time would slow the UI.
# --bpp per font: 4 = 16 edge shades, 2 = 4 shades (fine for big flat icons, half the flash)
CONV=(npx -y lv_font_conv@1.5.3 --no-compress --format lvgl --lv-include lvgl.h)

# Latin-1 + Latin Extended-A (Czech, Slovak, German, Polish...), dashes, quotes, bullet, ellipsis, euro
TEXT=0x20-0x7E,0xA0-0x17F,0x2013-0x2014,0x2018-0x201E,0x2022,0x2026,0x20AC
# LVGL's built-in LV_SYMBOL_* icons (same list as lvgl/scripts/.../built_in_font_gen.py)
SYMBOLS=61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650
# Extra Font Awesome icons: lock, star, search, history, moon, sun, clock, info, microphone, ruler,
# radio mast (FM Radio), globe (Web Radio)
EXTRA=61475,61445,61442,61914,61830,61829,61463,61530,61744,62791,62745,61612
# Weather Icons glyphs used by src/net/weather_codes.cpp
WEATHER=0xF00D,0xF02E,0xF002,0xF086,0xF013,0xF014,0xF01C,0xF019,0xF01A,0xF017,0xF01B,0xF01E,0xF009,0xF029,0xF00A,0xF02A,0xF010,0xF02D,0xF050,0xF07A,0xF051,0xF052,0xF055,0xF078,0xF07B,0xF084,0xF0B6

for size in 20 22 30; do  # 20: the smallest text (all 2 px up from 18 / 20 / 28 to read easily)
    "${CONV[@]}" --bpp 4 --size $size --lv-font-name font_ui_$size -o $OUT/font_ui_$size.c \
        --font Montserrat-Medium.ttf -r $TEXT \
        --font FontAwesome5-Solid+Brands+Regular.woff -r $SYMBOLS,$EXTRA
done

# Large: station names on Now Playing (any language), times, and only the
# icons used this big (home tiles: settings, tint, bell, bluetooth,
# microphone, ruler, radio mast, globe; the control centre's audio, wifi,
# upload and lock); each icon here costs ~1 KB, so add one when a screen needs it
SYMBOLS_50=61441,61459,61507,61683,61931,62099,61744,62791,61587,61475,62745,61612
"${CONV[@]}" --bpp 4 --size 50 --lv-font-name font_ui_50 -o $OUT/font_ui_50.c \
    --font Montserrat-Medium.ttf -r $TEXT \
    --font FontAwesome5-Solid+Brands+Regular.woff -r $SYMBOLS_50

# Clock digits, also used for the big temperature and the Level's angle (the ".")
for size in 96 140; do
    "${CONV[@]}" --bpp 4 --size $size --lv-font-name font_clock_$size -o $OUT/font_clock_$size.c \
        --font Montserrat-Medium.ttf -r 0x20,0x2D-0x2E,0x30-0x3A,0xB0
done

for size in 30 44; do
    "${CONV[@]}" --bpp 4 --size $size --lv-font-name font_weather_$size -o $OUT/font_weather_$size.c \
        --font weathericons-regular-webfont.ttf -r $WEATHER
done
# The one big weather icon (the Weather app, big card-face weather): the
# conditions only (src/net/weather_codes.cpp), not wind, sunrise and such
WEATHER_CONDITIONS=0xF00D,0xF02E,0xF002,0xF086,0xF013,0xF014,0xF01C,0xF019,0xF01A,0xF017,0xF01B,0xF01E,0xF009,0xF029,0xF00A,0xF02A,0xF010,0xF02D,0xF07B
"${CONV[@]}" --bpp 2 --size 120 --lv-font-name font_weather_120 -o $OUT/font_weather_120.c \
    --font weathericons-regular-webfont.ttf -r $WEATHER_CONDITIONS

# The Segment clock face: 7-segment digits, "88:88" about 390 px wide, "-" the middle bar
"${CONV[@]}" --bpp 4 --size 112 --lv-font-name font_seg_112 -o $OUT/font_seg_112.c \
    --font DSEG7Classic-Bold.ttf -r 0x20,0x2D,0x30-0x3A
