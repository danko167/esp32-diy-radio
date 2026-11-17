// LVGL 9 configuration. Anything not defined here uses LVGL's default
// (see lv_conf_internal.h).
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565

// Own allocator that always uses PSRAM, see src/ui/lv_mem_psram.c
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CUSTOM
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_DEF_REFR_PERIOD 16  // ~60 fps
// Note: LV_USE_OS + 2 draw units was measured slower here (task handoff
// overhead, and core 0 is busy with Wi-Fi), so rendering stays single-core.

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

// Own Montserrat builds with Latin-1/Extended-A (Czech etc.) and the degree
// sign, see tools/gen_fonts.sh. The built-in ones are ASCII only.
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(font_ui_20) LV_FONT_DECLARE(font_ui_22)
#define LV_FONT_DEFAULT &font_ui_22

#define LV_FONT_MONTSERRAT_14_ALIGNED 0  // unused, but linked in otherwise (29 KB)

// Fonts of clock faces on the microSD card: read into memory, then
// lv_binfont_create_from_buffer() loads them from there (src/ui/card_faces.cpp)
#define LV_USE_FS_MEMFS 1
#define LV_FS_MEMFS_LETTER 'M'

// Colour formats nothing here draws from or into, off to save flash. These
// switch off sources as well as targets: all drawing is RGB565; ARGB8888
// stays for LVGL's own layers (rounded clipping, bar indicators), RGB888 for
// gradients (lv_draw_sw_fill.c gives them as an RGB888 source; without it the
// weather's day bars crashed) and A8 for glyphs and shadows.
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 0
#define LV_DRAW_SW_SUPPORT_XRGB8888 0
#define LV_DRAW_SW_SUPPORT_ARGB8888_PREMULTIPLIED 0
#define LV_DRAW_SW_SUPPORT_L8 0
#define LV_DRAW_SW_SUPPORT_AL88 0
#define LV_DRAW_SW_SUPPORT_I1 0

// Widgets and extras the radio doesn't use, off to save flash. In use:
// arc (spinner), bar, button, buttonmatrix, image, label, line, roller,
// slider, spinner, switch, textarea.
#define LV_USE_ANIMIMG 0
#define LV_USE_ARCLABEL 0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 0
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0  // text_input.cpp has its own, on a button matrix
#define LV_USE_LED 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_SCALE 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0

#define LV_USE_SNAPSHOT 1  // screen transitions slide pre-rendered images

#define LV_USE_SYSMON 1
#define LV_USE_PERF_MONITOR 1
#ifdef PERF_LOG
#define LV_USE_PERF_MONITOR_LOG_MODE 1  // FPS/render/flush times to serial instead of on screen
#endif

#endif  // LV_CONF_H
