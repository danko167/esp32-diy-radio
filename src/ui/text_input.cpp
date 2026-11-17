// Full-screen text entry with an on-screen keyboard laid out like a phone's
// (design/src/screens/Keyboard.tsx): ten letters across, shift and backspace
// beside the bottom row, numbers and symbols on two more layers, the pressed
// key big in a bubble, and a letter's accents when holding it.

#include <string.h>

#include <src/widgets/buttonmatrix/lv_buttonmatrix_private.h>  // where the keys are

#include "screens.h"
#include "theme.h"

namespace ui::screens {
namespace {

constexpr uint32_t kKey = 0x3A3A40;
constexpr uint32_t kKeySpecial = theme::kSurfaceHi;  // shift, backspace, 123, ...
constexpr uint32_t kKeyPressed = 0x4A4A52;
constexpr int32_t kKeyboardHeight = 324;
constexpr int32_t kBubbleExtraWidth = 28;  // beyond the key's width
constexpr int32_t kBubbleMinWidth = 64;
constexpr uint32_t kDoubleShiftMs = 350;   // second tap within this: caps lock

// Keys with a meaning; any other label types itself
#define K_SHIFT LV_SYMBOL_UP
#define K_BACK LV_SYMBOL_BACKSPACE
#define K_OK LV_SYMBOL_OK
#define K_SPACE " "
#define K_NUMBERS "123"
#define K_LETTERS "ABC"
#define K_SYMBOLS "#+="
#define K_GAP " "  // hidden half keys that indent the middle letter row

// Width 2 = one letter; rows add up to 20. CLICK_TRIG: types on release, so
// sliding off a key types nothing. Backspace types on press and repeats.
constexpr lv_buttonmatrix_ctrl_t L = static_cast<lv_buttonmatrix_ctrl_t>(
    2 | LV_BUTTONMATRIX_CTRL_CLICK_TRIG | LV_BUTTONMATRIX_CTRL_NO_REPEAT);
constexpr lv_buttonmatrix_ctrl_t GAP = static_cast<lv_buttonmatrix_ctrl_t>(1 | LV_BUTTONMATRIX_CTRL_HIDDEN);
constexpr lv_buttonmatrix_ctrl_t wide(int width) {
    return static_cast<lv_buttonmatrix_ctrl_t>(width | LV_BUTTONMATRIX_CTRL_CLICK_TRIG |
                                               LV_BUTTONMATRIX_CTRL_NO_REPEAT);
}
constexpr lv_buttonmatrix_ctrl_t BACK = static_cast<lv_buttonmatrix_ctrl_t>(3);
constexpr lv_buttonmatrix_ctrl_t BACK4 = static_cast<lv_buttonmatrix_ctrl_t>(4);

const char *const kLower[] = {
    "q", "w", "e", "r", "t", "z", "u", "i", "o", "p", "\n",
    K_GAP, "a", "s", "d", "f", "g", "h", "j", "k", "l", K_GAP, "\n",
    K_SHIFT, "y", "x", "c", "v", "b", "n", "m", K_BACK, "\n",
    K_NUMBERS, ",", K_SPACE, ".", K_OK, ""};
const char *const kUpper[] = {
    "Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P", "\n",
    K_GAP, "A", "S", "D", "F", "G", "H", "J", "K", "L", K_GAP, "\n",
    K_SHIFT, "Y", "X", "C", "V", "B", "N", "M", K_BACK, "\n",
    K_NUMBERS, ",", K_SPACE, ".", K_OK, ""};
const lv_buttonmatrix_ctrl_t kLetterCtrl[] = {
    L, L, L, L, L, L, L, L, L, L,
    GAP, L, L, L, L, L, L, L, L, L, GAP,
    wide(3), L, L, L, L, L, L, L, BACK,
    wide(4), L, wide(8), L, wide(4)};

const char *const kNumbers[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    K_GAP, "-", "/", ":", ";", "(", ")", "&", "@", "\"", K_GAP, "\n",
    K_SYMBOLS, ".", ",", "?", "!", "'", "_", K_BACK, "\n",
    K_LETTERS, K_SPACE, K_OK, ""};
const char *const kSymbols[] = {
    "[", "]", "{", "}", "#", "%", "^", "*", "+", "=", "\n",
    K_GAP, "\\", "|", "~", "<", ">", "€", "£", "$", "•", K_GAP, "\n",
    K_NUMBERS, ".", ",", "?", "!", "'", "_", K_BACK, "\n",
    K_LETTERS, K_SPACE, K_OK, ""};
const lv_buttonmatrix_ctrl_t kSymbolCtrl[] = {
    L, L, L, L, L, L, L, L, L, L,
    GAP, L, L, L, L, L, L, L, L, L, GAP,
    wide(4), L, L, L, L, L, L, BACK4,
    wide(4), wide(12), wide(4)};

// Held letter: its accents, all in the fonts' Latin-1 and Extended-A
struct Accents {
    char letter;
    const char *lower[5];  // nullptr ends the list
    const char *upper[5];
};
const Accents kAccents[] = {
    {'a', {"á", "ä", "ą"}, {"Á", "Ä", "Ą"}},
    {'c', {"č", "ć"}, {"Č", "Ć"}},
    {'d', {"đ", "ď"}, {"Đ", "Ď"}},
    {'e', {"é", "ě", "ę", "ë"}, {"É", "Ě", "Ę", "Ë"}},
    {'i', {"í"}, {"Í"}},
    {'l', {"ł", "ľ"}, {"Ł", "Ľ"}},
    {'n', {"ň", "ń", "ñ"}, {"Ň", "Ń", "Ñ"}},
    {'o', {"ó", "ö", "ô"}, {"Ó", "Ö", "Ô"}},
    {'r', {"ř"}, {"Ř"}},
    {'s', {"š", "ś", "ß"}, {"Š", "Ś"}},
    {'t', {"ť"}, {"Ť"}},
    {'u', {"ú", "ů", "ü"}, {"Ú", "Ů", "Ü"}},
    {'y', {"ý"}, {"Ý"}},
    {'z', {"ž", "ź", "ż"}, {"Ž", "Ź", "Ż"}},
};

enum class Layer : uint8_t { Letters, Numbers, Symbols };
enum class Shift : uint8_t { Off, Once, Lock };

lv_obj_t *overlay;
lv_obj_t *textArea;
lv_obj_t *keyboard;
lv_obj_t *keyBubble;
lv_obj_t *keyBubbleLabel;
lv_obj_t *accentStrip;  // nullptr while closed
uint32_t shownKey;
Layer layer;
Shift shift;
bool autoCapital;       // names start with a capital; passwords don't
bool swallowRelease;    // the held letter that opened the accents
uint32_t lastShiftTap;
void (*submitCallback)(const char *);

bool is(const char *a, const char *b) { return strcmp(a, b) == 0; }

bool isSpecial(const char *t) {
    return is(t, K_SHIFT) || is(t, K_BACK) || is(t, K_OK) || is(t, K_NUMBERS) || is(t, K_LETTERS) ||
           is(t, K_SYMBOLS);
}

void showLayer() {
    if (layer == Layer::Letters) {
        lv_buttonmatrix_set_map(keyboard, shift == Shift::Off ? kLower : kUpper);
        lv_buttonmatrix_set_ctrl_map(keyboard, kLetterCtrl);
    } else {
        lv_buttonmatrix_set_map(keyboard, layer == Layer::Numbers ? kNumbers : kSymbols);
        lv_buttonmatrix_set_ctrl_map(keyboard, kSymbolCtrl);
    }
}

void setShift(Shift s) {
    if (s == shift) return;
    shift = s;
    showLayer();
}

void close() {
    lv_obj_delete_async(overlay);  // may be called from inside the keyboard's event
    overlay = nullptr;
}

void submit() {
    const char *text = lv_textarea_get_text(textArea);
    if (submitCallback) submitCallback(text);
    close();
}

void type(const char *s) {
    lv_textarea_add_text(textArea, s);
    if (shift == Shift::Once) setShift(Shift::Off);
}

// Key areas are relative to the keyboard; this is on screen
lv_area_t keyArea(uint32_t key) {
    lv_area_t kb;
    lv_obj_get_coords(keyboard, &kb);
    lv_area_t a = reinterpret_cast<lv_buttonmatrix_t *>(keyboard)->button_areas[key];
    lv_area_move(&a, kb.x1, kb.y1);
    return a;
}

int32_t clampX(int32_t x, int32_t w) { return LV_CLAMP(4, x, lv_obj_get_width(overlay) - w - 4); }

void closeAccents() {
    if (!accentStrip) return;
    lv_obj_delete(accentStrip);
    accentStrip = nullptr;
}

// Holding a letter: a row of its accents above it, tap one to type it
void openAccents(uint32_t key) {
    const char *text = lv_buttonmatrix_get_button_text(keyboard, key);
    const Accents *found = nullptr;
    for (const Accents &a : kAccents) {
        if (text[1] == '\0' && (text[0] | 0x20) == a.letter) found = &a;
    }
    if (!found) return;
    const char *const *options = shift == Shift::Off ? found->lower : found->upper;
    int count = 0;
    while (count < 5 && options[count]) count++;

    accentStrip = lv_obj_create(overlay);
    lv_obj_remove_style_all(accentStrip);
    lv_obj_set_size(accentStrip, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(accentStrip, 16, 0);
    lv_obj_set_style_bg_color(accentStrip, lv_color_hex(kKeyPressed), 0);
    lv_obj_set_style_bg_opa(accentStrip, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(accentStrip, 4, 0);
    lv_obj_set_style_pad_column(accentStrip, 4, 0);
    lv_obj_set_flex_flow(accentStrip, LV_FLEX_FLOW_ROW);
    lv_obj_set_scrollable(accentStrip, false);
    for (int i = 0; i < count; i++) {
        lv_obj_t *option = lv_obj_create(accentStrip);
        lv_obj_remove_style_all(option);
        lv_obj_set_size(option, 48, 64);
        lv_obj_set_style_radius(option, 12, 0);
        lv_obj_set_style_bg_color(option, lv_color_hex(i == 0 ? theme::accent() : kKeySpecial), 0);
        lv_obj_set_style_bg_opa(option, i == 0 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_opa(option, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_clickable(option, true);
        lv_obj_t *label = lv_label_create(option);
        lv_label_set_text(label, options[i]);
        lv_obj_set_style_text_font(label, &font_ui_30, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(i == 0 ? theme::kBg : theme::kText), 0);
        lv_obj_center(label);
        lv_obj_add_event_cb(
            option,
            [](lv_event_t *e) {
                type(lv_label_get_text(lv_obj_get_child(lv_event_get_target_obj(e), 0)));
                lv_obj_delete_async(accentStrip);  // we're inside its child's event
                accentStrip = nullptr;
            },
            LV_EVENT_CLICKED, nullptr);
    }
    const lv_area_t a = keyArea(key);
    const int32_t w = count * 52 + 4, h = 72;
    lv_obj_set_pos(accentStrip, clampX((a.x1 + a.x2) / 2 - w / 2, w), a.y1 - h - 8);
    swallowRelease = true;  // lifting the finger now mustn't type the plain letter
}

// Like a phone keyboard: the pressed letter shows big in a bubble above the
// finger, covering the key and as much again above it
void updateKeyBubble(lv_event_t *e) {
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) closeAccents();
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) swallowRelease = false;
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED &&
        code != LV_EVENT_PRESS_LOST && code != LV_EVENT_LONG_PRESSED) {
        return;
    }
    uint32_t key = lv_buttonmatrix_get_selected_button(keyboard);
    if (code == LV_EVENT_LONG_PRESSED && key != LV_BUTTONMATRIX_BUTTON_NONE && layer == Layer::Letters) {
        openAccents(key);
    }
    const bool held = code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING || code == LV_EVENT_LONG_PRESSED;
    if (!held || accentStrip || key == LV_BUTTONMATRIX_BUTTON_NONE) {
        key = LV_BUTTONMATRIX_BUTTON_NONE;
    } else {
        const char *t = lv_buttonmatrix_get_button_text(keyboard, key);
        if (isSpecial(t) || is(t, K_SPACE)) key = LV_BUTTONMATRIX_BUTTON_NONE;
    }
    if (key == shownKey) return;  // PRESSING repeats while held
    shownKey = key;
    lv_obj_set_hidden(keyBubble, key == LV_BUTTONMATRIX_BUTTON_NONE);
    if (key == LV_BUTTONMATRIX_BUTTON_NONE) return;

    const lv_area_t a = keyArea(key);
    const int32_t keyW = lv_area_get_width(&a), keyH = lv_area_get_height(&a);
    const int32_t w = LV_MAX(keyW + kBubbleExtraWidth, kBubbleMinWidth);
    lv_obj_set_size(keyBubble, w, keyH * 2);
    lv_obj_set_pos(keyBubble, clampX(a.x1 + keyW / 2 - w / 2, w), a.y2 + 1 - keyH * 2);
    lv_label_set_text(keyBubbleLabel, lv_buttonmatrix_get_button_text(keyboard, key));
}

void onKey(lv_event_t *e) {
    const uint32_t key = *static_cast<uint32_t *>(lv_event_get_param(e));
    if (swallowRelease) {
        swallowRelease = false;
        return;
    }
    const char *t = lv_buttonmatrix_get_button_text(keyboard, key);
    if (is(t, K_OK)) {
        submit();
    } else if (is(t, K_BACK)) {
        lv_textarea_delete_char(textArea);
        if (autoCapital && layer == Layer::Letters && lv_textarea_get_text(textArea)[0] == '\0') {
            setShift(Shift::Once);
        }
    } else if (is(t, K_SHIFT)) {
        // Tap: the next letter upper case. Double tap: caps lock.
        const bool quick = lv_tick_elaps(lastShiftTap) < kDoubleShiftMs;
        lastShiftTap = lv_tick_get();
        setShift(quick && shift == Shift::Once ? Shift::Lock : shift == Shift::Off ? Shift::Once : Shift::Off);
    } else if (is(t, K_NUMBERS) || is(t, K_LETTERS) || is(t, K_SYMBOLS)) {
        layer = is(t, K_NUMBERS) ? Layer::Numbers : is(t, K_LETTERS) ? Layer::Letters : Layer::Symbols;
        showLayer();
    } else {
        type(t);
    }
}

// Colours per key, which the button matrix's styles can't do
void colourKey(lv_event_t *e) {
    lv_draw_task_t *task = lv_event_get_draw_task(e);
    const auto *base = static_cast<lv_draw_dsc_base_t *>(lv_draw_task_get_draw_dsc(task));
    if (base->part != LV_PART_ITEMS) return;
    const char *t = lv_buttonmatrix_get_button_text(keyboard, base->id1);
    const bool pressed =
        lv_obj_has_state(keyboard, LV_STATE_PRESSED) && base->id1 == lv_buttonmatrix_get_selected_button(keyboard);
    const bool ok = is(t, K_OK);
    const bool shiftOn = is(t, K_SHIFT) && shift != Shift::Off;  // caps lock: accent
    const bool dark = !pressed && (ok || shiftOn);                // dark label on a light key

    if (lv_draw_task_get_type(task) == LV_DRAW_TASK_TYPE_FILL) {
        uint32_t colour = isSpecial(t) ? kKeySpecial : kKey;
        if (ok || (shiftOn && shift == Shift::Lock)) colour = theme::accent();
        if (shiftOn && shift == Shift::Once) colour = theme::kText;
        if (pressed) colour = kKeyPressed;
        lv_draw_task_get_fill_dsc(task)->color = lv_color_hex(colour);
    } else if (lv_draw_task_get_type(task) == LV_DRAW_TASK_TYPE_LABEL && dark) {
        lv_draw_task_get_label_dsc(task)->color = lv_color_hex(theme::kBg);
    }
}

}  // namespace

void openTextInput(lv_obj_t *screen, const char *title, bool password,
                   void (*onSubmit)(const char *text)) {
    submitCallback = onSubmit;

    overlay = lv_obj_create(screen);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, lv_color_hex(theme::kBg), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_clickable(overlay, true);
    lv_obj_set_scrollable(overlay, false);
    lv_obj_set_gesture_bubble(overlay, false);

    lv_obj_t *label = lv_label_create(overlay);
    lv_label_set_text(label, title);
    lv_obj_set_width(label, 320);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label, &font_ui_30, 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 20, 22);

    lv_obj_t *cancel = lv_button_create(overlay);
    lv_obj_set_style_bg_color(cancel, lv_color_hex(theme::kSurfaceHi), 0);
    lv_obj_set_style_shadow_width(cancel, 0, 0);
    lv_obj_set_style_radius(cancel, 22, 0);
    lv_obj_align(cancel, LV_ALIGN_TOP_RIGHT, -16, 14);
    lv_obj_add_event_cb(cancel, [](lv_event_t *) { close(); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *cancelLabel = lv_label_create(cancel);
    lv_label_set_text(cancelLabel, "Cancel");

    textArea = lv_textarea_create(overlay);
    lv_textarea_set_one_line(textArea, true);
    lv_textarea_set_password_mode(textArea, password);
    lv_obj_set_width(textArea, password ? 376 : 448);
    lv_obj_align(textArea, LV_ALIGN_TOP_LEFT, 16, 84);
    lv_obj_set_style_bg_color(textArea, lv_color_hex(theme::kSurface), 0);
    lv_obj_set_style_radius(textArea, 16, 0);
    lv_obj_add_state(textArea, LV_STATE_FOCUSED);  // show the cursor

    if (password) {
        // Show/hide toggle: typing a long password blind on a small keyboard is no fun
        lv_obj_t *eye = lv_button_create(overlay);
        lv_obj_set_size(eye, 60, 52);
        lv_obj_align(eye, LV_ALIGN_TOP_RIGHT, -16, 84);
        lv_obj_set_style_bg_color(eye, lv_color_hex(theme::kSurfaceHi), 0);
        lv_obj_set_style_shadow_width(eye, 0, 0);
        lv_obj_set_style_radius(eye, 16, 0);
        lv_obj_t *icon = lv_label_create(eye);
        lv_label_set_text(icon, LV_SYMBOL_EYE_OPEN);
        lv_obj_center(icon);
        lv_obj_add_event_cb(
            eye,
            [](lv_event_t *e) {
                const bool hidden = lv_textarea_get_password_mode(textArea);
                lv_textarea_set_password_mode(textArea, !hidden);
                lv_obj_t *icon = lv_obj_get_child(lv_event_get_target_obj(e), 0);
                lv_label_set_text(icon, hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
            },
            LV_EVENT_CLICKED, nullptr);
    }

    keyboard = lv_buttonmatrix_create(overlay);
    lv_obj_set_size(keyboard, LV_PCT(100), kKeyboardHeight);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_gesture_bubble(keyboard, false);
    lv_obj_set_style_bg_color(keyboard, lv_color_hex(theme::kSurface), 0);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(keyboard, 0, 0);
    lv_obj_set_style_border_width(keyboard, 0, 0);
    lv_obj_set_style_pad_all(keyboard, 6, 0);
    lv_obj_set_style_pad_row(keyboard, 8, 0);
    lv_obj_set_style_pad_column(keyboard, 5, 0);
    lv_obj_set_style_radius(keyboard, 10, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_border_width(keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_font(keyboard, &font_ui_30, LV_PART_ITEMS);
    lv_obj_set_style_text_color(keyboard, lv_color_hex(theme::kText), LV_PART_ITEMS);
    // The theme darkens pressed keys with a filter; colourKey() does it instead
    lv_obj_set_style_color_filter_opa(keyboard, LV_OPA_TRANSP,
                                      static_cast<lv_style_selector_t>(LV_PART_ITEMS) | LV_STATE_PRESSED);
    lv_obj_set_send_draw_task_events(keyboard, true);
    lv_obj_add_event_cb(keyboard, colourKey, LV_EVENT_DRAW_TASK_ADDED, nullptr);
    lv_obj_add_event_cb(keyboard, onKey, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(keyboard, updateKeyBubble, LV_EVENT_ALL, nullptr);

    layer = Layer::Letters;
    autoCapital = !password;
    shift = autoCapital ? Shift::Once : Shift::Off;
    swallowRelease = false;
    accentStrip = nullptr;
    lastShiftTap = 0;
    showLayer();

    keyBubble = lv_obj_create(overlay);  // after the keyboard, so on top of it
    lv_obj_remove_style_all(keyBubble);
    lv_obj_set_style_radius(keyBubble, 16, 0);
    lv_obj_set_style_bg_color(keyBubble, lv_color_hex(kKeyPressed), 0);
    lv_obj_set_style_bg_opa(keyBubble, LV_OPA_COVER, 0);
    lv_obj_set_clickable(keyBubble, false);
    lv_obj_set_scrollable(keyBubble, false);
    lv_obj_set_hidden(keyBubble, true);
    keyBubbleLabel = lv_label_create(keyBubble);
    lv_obj_set_style_text_font(keyBubbleLabel, &font_ui_50, 0);
    lv_obj_set_style_text_color(keyBubbleLabel, lv_color_hex(theme::kText), 0);
    lv_obj_align(keyBubbleLabel, LV_ALIGN_TOP_MID, 0, 10);
    shownKey = LV_BUTTONMATRIX_BUTTON_NONE;
}

}  // namespace ui::screens
