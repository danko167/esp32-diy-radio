// Building blocks from the designs in design/src/components.tsx

#include <src/core/lv_obj_event_private.h>  // lv_hit_test_info_t

#include <initializer_list>

#include "screens.h"
#include "theme.h"

namespace ui::screens {
namespace {

// Grid templates must outlive the objects that use them. One long list of
// each, ending in the terminator: a grid of n uses its last n entries.
constexpr int kMaxColumns = 5;
constexpr int kMaxRows = 24;

struct Templates {
    int32_t columns[kMaxColumns + 1];
    int32_t rows[kMaxRows + 1];
    Templates() {
        for (int i = 0; i < kMaxColumns; i++) columns[i] = LV_GRID_FR(1);
        for (int i = 0; i < kMaxRows; i++) rows[i] = LV_GRID_CONTENT;
        columns[kMaxColumns] = rows[kMaxRows] = LV_GRID_TEMPLATE_LAST;
    }
};
const Templates kTemplates;

void styleChip(lv_obj_t *chip, bool selected, bool outlined) {
    if (outlined) {
        lv_obj_set_style_bg_opa(chip, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(chip, 2, 0);
        lv_obj_set_style_border_color(chip, lv_color_hex(selected ? theme::accent() : theme::kSurfaceHi), 0);
        lv_obj_set_style_text_color(chip, lv_color_hex(selected ? theme::accent() : theme::kText), 0);
    } else {
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(chip, lv_color_hex(selected ? theme::accent() : theme::kSurfaceHi), 0);
        lv_obj_set_style_text_color(chip, lv_color_hex(selected ? theme::kBg : theme::kText), 0);
    }
}

// A big slider ignores a touch that moves mostly up or down: a swipe that
// starts on it (closing the control centre, scrolling a page) leaves the value
// alone. Decided once the finger has moved scroll_limit (10 px); a tap that
// doesn't move still sets the value where it lands, sideways drags as ever.
// One finger: one press at a time.
constexpr int32_t kSliderDecidePx = 10;  // LVGL's scroll limit
lv_point_t sliderPressAt;
bool sliderDecided = false, sliderVertical = false;

void filterSliderTouch(lv_event_t *e) {
    lv_indev_t *indev = lv_indev_active();
    if (!indev) return;
    switch (lv_event_get_code(e)) {
        case LV_EVENT_PRESSED:
            lv_indev_get_point(indev, &sliderPressAt);
            sliderDecided = sliderVertical = false;
            break;
        case LV_EVENT_PRESSING:
            if (!sliderDecided) {
                lv_point_t p;
                lv_indev_get_point(indev, &p);
                const int32_t dx = LV_ABS(p.x - sliderPressAt.x), dy = LV_ABS(p.y - sliderPressAt.y);
                if (LV_MAX(dx, dy) >= kSliderDecidePx) {
                    sliderDecided = true;
                    sliderVertical = dy > dx;
                }
            }
            if (sliderVertical || !sliderDecided) lv_event_stop_processing(e);
            break;
        case LV_EVENT_RELEASED:
        case LV_EVENT_PRESS_LOST:
            if (sliderVertical) lv_event_stop_processing(e);  // the release would set it there
            break;
        default:
            break;
    }
}

struct Chips {
    void (*onPick)(int);
    bool outlined;
};

void updateBigSlider(lv_obj_t *slider) {
    const int32_t v = lv_slider_get_value(slider);
    const int32_t min = lv_slider_get_min_value(slider), max = lv_slider_get_max_value(slider);
    lv_obj_t *icon = lv_obj_get_child(slider, 0);
    lv_obj_t *value = lv_obj_get_child(slider, 1);
    // The icon sits on the fill once there's enough of it
    const bool onFill = (v - min) * 100 > (max - min) * 12;
    if (icon) lv_obj_set_style_text_color(icon, lv_color_hex(onFill ? theme::kBg : theme::kText), 0);
    if (value) lv_label_set_text_fmt(value, "%ld%%", (long)v);
}

}  // namespace

void setGrid(lv_obj_t *obj, int columns, int rows) {
    columns = LV_CLAMP(1, columns, kMaxColumns);
    rows = LV_CLAMP(1, rows, kMaxRows);
    lv_obj_set_layout(obj, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(obj, kTemplates.columns + kMaxColumns - columns, kTemplates.rows + kMaxRows - rows);
}

lv_obj_t *addBigSlider(lv_obj_t *parent, int32_t min, int32_t max, int32_t value, const char *icon,
                       lv_event_cb_t onChange) {
    lv_obj_t *s = lv_slider_create(parent);
    lv_obj_set_size(s, LV_PCT(100), 58);
    lv_slider_set_range(s, min, max);
    lv_slider_set_value(s, value, LV_ANIM_OFF);
    lv_obj_set_style_radius(s, 28, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, lv_color_hex(theme::kSurfaceHi), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s, 28, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, lv_color_hex(theme::accent()), LV_PART_INDICATOR);
    // No knob: drag or tap anywhere on the bar
    lv_obj_set_style_bg_opa(s, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(s, 0, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s, 0, LV_PART_KNOB);
    lv_obj_set_style_outline_width(s, 0, LV_STATE_FOCUSED);
    lv_obj_set_gesture_bubble(s, false);

    lv_obj_t *iconLabel = lv_label_create(s);
    lv_label_set_text(iconLabel, icon ? icon : "");
    lv_obj_align(iconLabel, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_t *valueLabel = lv_label_create(s);
    lv_obj_set_style_text_color(valueLabel, lv_color_hex(theme::kText), 0);
    lv_obj_align(valueLabel, LV_ALIGN_RIGHT_MID, -20, 0);
    updateBigSlider(s);

    // LVGL only lets the knob be grabbed; here the whole bar can be
    lv_obj_add_event_cb(
        s,
        [](lv_event_t *e) {
            auto *info = static_cast<lv_hit_test_info_t *>(lv_event_get_param(e));
            lv_area_t a;
            lv_obj_get_coords(lv_event_get_target_obj(e), &a);
            const lv_point_t *p = info->point;
            info->res = p->x >= a.x1 && p->x <= a.x2 && p->y >= a.y1 && p->y <= a.y2;
        },
        LV_EVENT_HIT_TEST, nullptr);
    for (lv_event_code_t code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST}) {
        lv_obj_add_event_cb(s, filterSliderTouch, static_cast<lv_event_code_t>(code | LV_EVENT_PREPROCESS), nullptr);
    }
    lv_obj_add_event_cb(s, [](lv_event_t *e) { updateBigSlider(lv_event_get_target_obj(e)); },
                        LV_EVENT_VALUE_CHANGED, nullptr);
    if (onChange) lv_obj_add_event_cb(s, onChange, LV_EVENT_VALUE_CHANGED, nullptr);
    return s;
}

lv_obj_t *addChips(lv_obj_t *parent, const char *const *labels, int count, int columns, int selected,
                   bool outlined, int32_t height, void (*onPick)(int index)) {
    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_gap(grid, 8, 0);
    lv_obj_set_scrollable(grid, false);
    setGrid(grid, columns, (count + columns - 1) / columns);
    lv_obj_set_user_data(grid, new Chips{onPick, outlined});
    lv_obj_add_event_cb(
        grid, [](lv_event_t *e) { delete static_cast<Chips *>(lv_obj_get_user_data(lv_event_get_target_obj(e))); },
        LV_EVENT_DELETE, nullptr);

    for (int i = 0; i < count; i++) {
        lv_obj_t *chip = lv_obj_create(grid);
        lv_obj_remove_style_all(chip);
        lv_obj_set_height(chip, height);
        lv_obj_set_style_radius(chip, height / 2, 0);
        lv_obj_set_grid_cell(chip, LV_GRID_ALIGN_STRETCH, i % columns, 1, LV_GRID_ALIGN_CENTER, i / columns, 1);
        lv_obj_set_clickable(chip, true);
        lv_obj_set_scrollable(chip, false);
        styleChip(chip, i == selected, outlined);
        lv_obj_t *l = lv_label_create(chip);
        lv_label_set_text(l, labels[i]);
        lv_obj_center(l);
        lv_obj_add_event_cb(
            chip,
            [](lv_event_t *e) {
                lv_obj_t *chip = lv_event_get_target_obj(e);
                lv_obj_t *grid = lv_obj_get_parent(chip);
                const int index = lv_obj_get_index(chip);
                selectChip(grid, index);
                const Chips *c = static_cast<Chips *>(lv_obj_get_user_data(grid));
                if (c->onPick) c->onPick(index);
            },
            LV_EVENT_CLICKED, nullptr);
    }
    return grid;
}

void selectChip(lv_obj_t *chips, int index) {
    const Chips *c = static_cast<Chips *>(lv_obj_get_user_data(chips));
    for (uint32_t i = 0; i < lv_obj_get_child_count(chips); i++) {
        styleChip(lv_obj_get_child(chips, i), (int)i == index, c->outlined);
    }
}

lv_obj_t *addSwitch(lv_obj_t *parent, bool on, lv_event_cb_t onChange) {
    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_set_size(sw, 66, 38);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_set_gesture_bubble(sw, false);
    lv_obj_add_event_cb(sw, onChange, LV_EVENT_VALUE_CHANGED, nullptr);
    return sw;
}

lv_obj_t *addRoller(lv_obj_t *parent, const char *options, lv_roller_mode_t mode, int selected, int32_t width,
                    const lv_font_t *font) {
    lv_obj_t *r = lv_roller_create(parent);
    lv_roller_set_options(r, options, mode);
    lv_roller_set_selected(r, selected, LV_ANIM_OFF);
    lv_obj_set_width(r, width);
    lv_obj_set_style_text_font(r, font, 0);
    lv_obj_set_style_text_color(r, lv_color_hex(theme::kTextDim), 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(theme::kSurface), 0);
    lv_obj_set_style_radius(r, font == &font_ui_50 ? 20 : 16, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(theme::kSurfaceHi), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_radius(r, font == &font_ui_50 ? 16 : 12, LV_PART_SELECTED);
    lv_obj_set_style_text_color(r, lv_color_hex(theme::kText), LV_PART_SELECTED);
    lv_roller_set_visible_row_count(r, 3);  // after the font: the height comes from it
    lv_obj_set_gesture_bubble(r, false);
    return r;
}

lv_obj_t *addHint(lv_obj_t *parent, const char *text, int32_t lineSpace) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, &font_ui_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(theme::kTextDim), 0);
    lv_obj_set_style_text_line_space(label, lineSpace, 0);
    lv_obj_set_style_pad_hor(label, 8, 0);
    lv_obj_set_style_pad_ver(label, 4, 0);
    return label;
}

lv_obj_t *addIconCircle(lv_obj_t *parent, const char *symbol, int32_t size, uint32_t colour, lv_opa_t bgOpa,
                        uint32_t iconColour) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, size, size);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(colour), 0);
    lv_obj_set_style_bg_opa(c, bgOpa, 0);
    lv_obj_set_clickable(c, false);
    lv_obj_set_scrollable(c, false);
    lv_obj_t *l = lv_label_create(c);
    lv_label_set_text(l, symbol);
    lv_obj_set_style_text_color(l, lv_color_hex(iconColour), 0);
    lv_obj_center(l);
    return c;
}

lv_obj_t *addRow(lv_obj_t *parent, int32_t gap) {
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, gap, 0);
    lv_obj_set_scrollable(r, false);
    lv_obj_set_clickable(r, false);
    return r;
}

lv_obj_t *addColumn(lv_obj_t *parent, int32_t gap) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, gap, 0);
    lv_obj_set_scrollable(c, false);
    lv_obj_set_clickable(c, false);
    return c;
}

lv_obj_t *addText(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t colour) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(colour), 0);
    return label;
}

lv_obj_t *addPill(lv_obj_t *parent, const char *label, bool primary, int32_t height, lv_event_cb_t onTap) {
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_height(btn, height);
    lv_obj_set_style_radius(btn, height / 2, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_hor(btn, 24, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(primary ? theme::accent() : theme::kSurfaceHi), 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(primary ? theme::kBg : theme::kText), 0);
    if (onTap) lv_obj_add_event_cb(btn, onTap, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *l = lv_label_create(btn);
    lv_label_set_text(l, label);
    lv_obj_center(l);
    return btn;
}

lv_obj_t *addRoundButton(lv_obj_t *parent, int32_t size, const char *symbol, bool primary,
                         lv_event_cb_t onTap, void *userData) {
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(primary ? theme::accent() : theme::kSurfaceHi), 0);
    if (primary) lv_obj_set_style_text_color(btn, lv_color_hex(theme::kBg), 0);
    if (onTap) lv_obj_add_event_cb(btn, onTap, LV_EVENT_CLICKED, userData);
    lv_obj_t *icon = lv_label_create(btn);
    lv_label_set_text(icon, symbol);
    lv_obj_center(icon);
    return btn;
}

}  // namespace ui::screens
