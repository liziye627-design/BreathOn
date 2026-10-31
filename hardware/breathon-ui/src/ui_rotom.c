#include "ui_internal.h"
#include "ui_timeline.h"

static lv_obj_t *sprite_group;
static lv_obj_t *glow;
static lv_obj_t *eyes[2];
static lv_obj_t *pupils[2];
static lv_obj_t *mouth;
static lv_obj_t *smile;
static lv_obj_t *teeth[2];
static ui_pose_t previous_pose;
static ui_mood_t previous_mood;
static bool pose_valid;

static lv_obj_t *shape(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *object = ui_panel(parent, x, y, w, h, color);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
    return object;
}

static void lightning(const lv_point_precise_t *points, uint32_t count)
{
    lv_obj_t *line = lv_line_create(sprite_group);
    lv_obj_remove_style_all(line);
    lv_line_set_points(line, points, count);
    lv_obj_set_style_line_color(line, lv_color_hex(UI_CYAN), 0);
    lv_obj_set_style_line_width(line, 7, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
}

void ui_rotom_create(lv_obj_t *parent)
{
    pose_valid = false;
    /* Static point storage outlives LVGL lines; no frame owns heap memory. */
    static const lv_point_precise_t left[] = {{28, 62}, {18, 47}, {27, 44}};
    static const lv_point_precise_t right[] = {{92, 62}, {102, 47}, {93, 44}};
    glow = shape(parent, 29, 99, 62, 9, UI_CYAN);
    sprite_group = shape(parent, 0, 0, 120, 104, UI_BG);
    lv_obj_set_style_bg_opa(sprite_group, LV_OPA_TRANSP, 0);
    lightning(left, 3);
    lightning(right, 3);
    shape(sprite_group,54,6,12,20,UI_ORANGE);
    lv_obj_t *body = shape(sprite_group, 25, 17, 70, 82, UI_ORANGE_LIGHT);
    lv_obj_set_style_radius(body, 20, 0);
    lv_obj_set_style_bg_grad_color(body, lv_color_hex(UI_ORANGE), 0);
    lv_obj_set_style_bg_grad_dir(body, LV_GRAD_DIR_VER, 0);
    lv_obj_t *shine = shape(body, 14, 8, 19, 3, UI_BLUSH);
    lv_obj_set_style_bg_opa(shine, LV_OPA_70, 0);
    for (int i = 0; i < 2; ++i) {
        eyes[i] = shape(body, 8 + i * 33, 21, 21, 14, UI_WHITE);
        lv_obj_set_style_border_color(eyes[i], lv_color_hex(UI_CYAN), 0);
        lv_obj_set_style_border_width(eyes[i], 2, 0);
        pupils[i] = shape(eyes[i], 6, 2, 7, 9, UI_INK);
        shape(pupils[i], 1, 0, 2, 3, UI_WHITE);
        shape(body, 6 + i * 48, 42, 10, 4, UI_BLUSH);
    }
    smile = lv_arc_create(body);
    lv_obj_remove_style_all(smile);
    lv_obj_set_pos(smile, 23, 27);
    lv_obj_set_size(smile, 24, 24);
    lv_arc_set_bg_angles(smile, 20, 160);
    lv_obj_set_style_arc_color(smile, lv_color_hex(UI_INK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(smile, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(smile, true, LV_PART_MAIN);
    lv_obj_remove_flag(smile, LV_OBJ_FLAG_CLICKABLE);
    mouth = shape(body, 27, 40, 16, 6, UI_INK);
    for (int i = 0; i < 2; ++i) teeth[i] = shape(body, 27 + i * 12, 42, 4, 4, UI_WHITE);
    lv_obj_t *screen = shape(body, 10, 55, 50, 18, UI_INK);
    lv_obj_set_style_radius(screen, 6, 0);
    static const lv_point_precise_t breath[] = {{0, 8}, {7, 8}, {12, 3}, {18, 13}, {24, 8}, {32, 8}};
    lv_obj_t *mark = lv_line_create(screen);
    lv_obj_remove_style_all(mark);
    lv_obj_set_pos(mark, 9, 1);
    lv_line_set_points(mark, breath, sizeof(breath) / sizeof(breath[0]));
    lv_obj_set_style_line_color(mark, lv_color_hex(UI_CYAN), 0);
    lv_obj_set_style_line_width(mark, 2, 0);
    lv_obj_set_style_line_rounded(mark, true, 0);
    lv_obj_remove_flag(mark, LV_OBJ_FLAG_CLICKABLE);
}

void ui_rotom_frame(uint32_t elapsed_ms, ui_mood_t mood)
{
    ui_pose_t pose = ui_timeline_pose(BREATHON_UI_REDUCED_MOTION ? 0 : elapsed_ms, mood);
    if (pose_valid && previous_mood == mood && previous_pose.float_y == pose.float_y &&
        previous_pose.eye_height == pose.eye_height && previous_pose.mouth_height == pose.mouth_height &&
        previous_pose.glow_opa == pose.glow_opa) return;
    previous_pose = pose;
    previous_mood = mood;
    pose_valid = true;
    lv_obj_set_y(sprite_group, pose.float_y);
    lv_obj_set_style_bg_opa(glow, pose.glow_opa, 0);
    for (int i = 0; i < 2; ++i) {
        lv_obj_set_height(eyes[i], pose.eye_height);
        lv_obj_set_y(eyes[i], 28 - pose.eye_height / 2);
        lv_obj_set_height(pupils[i], pose.eye_height > 5 ? pose.eye_height - 4 : 1);
        if (pose.eye_height < 6) lv_obj_add_flag(pupils[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(pupils[i], LV_OBJ_FLAG_HIDDEN);
    }
    bool speaking = mood == UI_MOOD_SPEAK;
    if (speaking) {
        lv_obj_add_flag(smile, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(mouth, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_height(mouth, pose.mouth_height);
    } else {
        lv_obj_remove_flag(smile, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mouth, LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < 2; ++i) lv_obj_set_y(teeth[i], speaking ? 40 : 42);
}
