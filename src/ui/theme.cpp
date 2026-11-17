#include "theme.h"

#include <src/core/lv_obj_private.h>        // an object's styles, for recolour()
#include <src/core/lv_obj_style_private.h>

#include "app/settings.h"

namespace theme {

const Accent kAccents[kAccentCount] = {
    {"Sky", 0x5AC8FA},  // the original one, and the default
    {"Mint", 0x63E6E2},
    {"Green", 0x30D158},
    {"Yellow", 0xFFD60A},
    {"Orange", 0xFF9F0A},
    {"Red", 0xFF453A},
    {"Pink", 0xFF375F},
    {"Purple", 0xBF5AF2},
};

namespace {

constexpr uint32_t kSecondary = 0x7A7AFF;  // the default theme's second colour

lv_style_t pressed;
lv_theme_t *extended;

// The default theme darkens pressed buttons with a colour filter, which
// draws wrongly (green, shifted) into the snapshots that slides use. Dim
// their background instead: same feel, plain drawing.
void apply(lv_theme_t *, lv_obj_t *obj) {
    if (lv_obj_check_type(obj, &lv_button_class)) lv_obj_add_style(obj, &pressed, LV_STATE_PRESSED);
}

// Again with another accent, the default theme refills its styles in place,
// so objects already using them only need telling (applyAccent)
void initThemes() {
    lv_display_t *disp = lv_display_get_default();
    lv_theme_t *base = lv_theme_default_init(disp, lv_color_hex(accent()), lv_color_hex(kSecondary),
                                             true /* dark */, &font_ui_22);
    // A child theme of the default one, adding the style above (lives forever)
    if (!extended) extended = lv_theme_create();
    lv_theme_copy(extended, base);  // its primary colour too, for lv_theme_get_color_primary()
    lv_theme_set_parent(extended, base);
    lv_theme_set_apply_cb(extended, apply);
}

}  // namespace

int accentIndex() {
    const uint8_t i = settings::get().accent;
    return i < kAccentCount ? i : 0;
}

uint32_t accent() { return kAccents[accentIndex()].colour; }

void begin() {
    lv_style_init(&pressed);
    lv_style_set_color_filter_opa(&pressed, LV_OPA_TRANSP);
    lv_style_set_bg_opa(&pressed, LV_OPA_60);
    initThemes();
    lv_display_set_theme(lv_display_get_default(), extended);
}

void applyAccent() {
    initThemes();
    lv_obj_report_style_change(nullptr);  // all screens and layers
}

void recolour(lv_obj_t *obj, uint32_t from, uint32_t to) {
    static const lv_style_prop_t kColours[] = {
        LV_STYLE_TEXT_COLOR,    LV_STYLE_BG_COLOR,     LV_STYLE_BG_GRAD_COLOR, LV_STYLE_BORDER_COLOR,
        LV_STYLE_OUTLINE_COLOR, LV_STYLE_SHADOW_COLOR, LV_STYLE_LINE_COLOR,    LV_STYLE_ARC_COLOR,
        LV_STYLE_IMAGE_RECOLOR,
    };
    const lv_color_t old = lv_color_hex(from);
    for (uint32_t i = 0; i < obj->style_cnt; i++) {
        if (!obj->styles[i].is_local) continue;
        const lv_style_t *style = obj->styles[i].style;
        const lv_style_selector_t selector = obj->styles[i].selector;
        for (lv_style_prop_t prop : kColours) {
            lv_style_value_t v;
            if (lv_style_get_prop(style, prop, &v) == LV_STYLE_RES_FOUND && lv_color_eq(v.color, old)) {
                v.color = lv_color_hex(to);
                lv_obj_set_local_style_prop(obj, prop, v, selector);  // same style: no new entry
            }
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(obj); i++) recolour(lv_obj_get_child(obj, i), from, to);
}

}  // namespace theme
