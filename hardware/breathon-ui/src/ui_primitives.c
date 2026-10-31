#include "ui_internal.h"
#include <stdint.h>

lv_obj_t *ui_text(lv_obj_t *parent, const char *text, int x, int y, int w)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, w);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

lv_obj_t *ui_panel(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(panel, lv_color_hex(UI_INK), 0);
    lv_obj_set_style_radius(panel, 18, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}

static void navigate(lv_event_t *event)
{
    breathon_ui_show((breathon_page_t)(intptr_t)lv_event_get_user_data(event));
}

lv_obj_t *ui_button(const char *text, int x, int y, int w, bool primary, breathon_page_t target)
{
    return ui_action_button(text, x, y, w, primary, navigate, (void *)(intptr_t)target);
}

lv_obj_t *ui_action_button(const char *text, int x, int y, int w, bool primary,
                           lv_event_cb_t callback, void *data)
{
    lv_obj_t *button = lv_button_create(ui_root);
    /* Own all button states: the LVGL theme's delayed color/filter transitions
     * can otherwise make a newly disabled action still look available. */
    lv_obj_remove_style_all(button);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, 44);
    lv_obj_set_style_radius(button, 22, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(primary ? UI_BUTTON : UI_WHITE), 0);
    lv_obj_set_style_bg_grad_color(button, lv_color_hex(primary ? UI_BUTTON_DARK : UI_PALE), 0);
    lv_obj_set_style_bg_grad_dir(button, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_text_color(button, lv_color_hex(primary ? UI_PRESS_INK : UI_INK), 0);
    lv_obj_set_style_border_color(button, lv_color_hex(UI_BORDER), 0);
    lv_obj_set_style_border_width(button, primary ? 0 : 1, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_BUTTON_DARK), LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_color(button, lv_color_hex(UI_BUTTON_DARK), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(button, lv_color_hex(UI_PRESS_INK), LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(button, lv_color_hex(UI_TEXT), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_width(button, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_PALE), LV_STATE_DISABLED);
    lv_obj_set_style_bg_grad_color(button, lv_color_hex(UI_PALE), LV_STATE_DISABLED);
    lv_obj_set_style_text_color(button, lv_color_hex(UI_MUTED), LV_STATE_DISABLED);
    lv_obj_set_style_opa(button, LV_OPA_COVER, LV_STATE_DISABLED);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, data);
    return button;
}

void ui_header(const char *title)
{
    lv_obj_t *badge = ui_panel(ui_root, 12, 9, 26, 26, UI_ORANGE_LIGHT);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    ui_face(badge, 26);
    lv_obj_t *label = ui_text(ui_root, title, 12, 12, 216);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

void ui_soft_button(lv_obj_t *button, bool primary)
{
    lv_obj_set_style_radius(button, 22, 0);
    lv_obj_set_style_bg_grad_dir(button, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(primary ? UI_BUTTON : UI_WHITE), 0);
}

void ui_orb(int x, int y, int size)
{
    lv_obj_t *halo = ui_panel(ui_root, x - 6, y - 6, size + 12, size + 12, UI_WASH);
    lv_obj_set_style_radius(halo, LV_RADIUS_CIRCLE, 0);
    lv_obj_t *orb = ui_panel(ui_root, x, y, size, size, UI_ORANGE_LIGHT);
    lv_obj_set_style_radius(orb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_grad_color(orb, lv_color_hex(UI_ORANGE), 0);
    lv_obj_set_style_bg_grad_dir(orb, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(orb, 2, 0);
    lv_obj_set_style_border_color(orb, lv_color_hex(UI_WHITE), 0);
    lv_obj_t *shine = ui_panel(orb, size / 5, size / 7, size / 3, 5, UI_WHITE);
    lv_obj_set_style_bg_opa(shine, LV_OPA_50, 0);
    ui_face(orb, size);
}

ui_face_t ui_face(lv_obj_t *parent, int size)
{
    ui_face_t face = {0};
    int eye_w = size > 40 ? size / 16 : 2;
    int eye_h = size > 40 ? size / 12 : 3;
    for (int i = 0; i < 2; ++i) {
        face.eyes[i] = ui_panel(parent, size * (32 + 31 * i) / 100,
                               size * 43 / 100, eye_w, eye_h, UI_INK);
        if (size > 40) ui_panel(face.eyes[i], 1, 1, 2, 2, UI_WHITE);
        lv_obj_t *cheek = ui_panel(parent, size * (18 + 56 * i) / 100,
                                   size * 57 / 100, size / 9, size / 14 + 1, UI_BLUSH);
        lv_obj_set_style_bg_opa(cheek, LV_OPA_70, 0);
    }
    lv_obj_t *smile = lv_arc_create(parent);
    face.mouth = smile;
    lv_obj_set_size(smile, size / 4, size / 4);
    lv_obj_set_pos(smile, size * 3 / 8, size * 9 / 20);
    lv_arc_set_bg_angles(smile, 25, 155);
    lv_obj_set_style_arc_color(smile, lv_color_hex(UI_INK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(smile, size > 40 ? 3 : 1, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(smile, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_remove_style(smile, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(smile, LV_OBJ_FLAG_CLICKABLE);
    return face;
}
