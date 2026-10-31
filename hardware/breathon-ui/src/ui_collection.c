#include "ui_internal.h"

static lv_timer_t *collect_timer;
static lv_obj_t *progress_ring;
static lv_obj_t *time_label;
static uint32_t start_tick;
static const lv_point_precise_t wave_points[] = {
    {0, 18}, {14, 18}, {22, 5}, {28, 5}, {40, 32}, {46, 32}, {54, 18}, {72, 18}
};

static void collect_tick(lv_timer_t *timer)
{
    (void)timer;
    uint32_t elapsed = lv_tick_elaps(start_tick);
    if (elapsed >= 5000) {
        breathon_ui_show(BREATHON_RESULT);
        return;
    }
    lv_arc_set_value(progress_ring, (int32_t)(elapsed / 50));
    lv_label_set_text_fmt(time_label, "%lu / 5 秒", (unsigned long)(elapsed / 1000));
}

void ui_collect_cancel(void)
{
    if (collect_timer) {
        lv_timer_delete(collect_timer);
        collect_timer = NULL;
    }
}

void ui_collect(void)
{
    ui_header("检测体验");
    progress_ring = lv_arc_create(ui_root);
    lv_obj_set_size(progress_ring, 148, 148);
    lv_obj_set_pos(progress_ring, 46, 48);
    lv_arc_set_rotation(progress_ring, 270);
    lv_arc_set_bg_angles(progress_ring, 0, 360);
    lv_arc_set_range(progress_ring, 0, 100);
    lv_arc_set_value(progress_ring, 0);
    lv_obj_set_style_arc_color(progress_ring, lv_color_hex(UI_PALE), LV_PART_MAIN);
    lv_obj_set_style_arc_color(progress_ring, lv_color_hex(UI_CYAN), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(progress_ring, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(progress_ring, 12, LV_PART_INDICATOR);
    lv_obj_remove_style(progress_ring, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(progress_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *wave = lv_line_create(ui_root);
    lv_line_set_points(wave, wave_points, sizeof(wave_points) / sizeof(wave_points[0]));
    lv_obj_set_pos(wave, 84, 84);
    lv_obj_set_style_line_color(wave, lv_color_hex(UI_CYAN), 0);
    lv_obj_set_style_line_width(wave, 3, 0);
    lv_obj_set_style_line_rounded(wave, true, 0);
    time_label = ui_text(ui_root, "0 / 5 秒", 66, 132, 108);
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *caption = ui_text(ui_root, "仅体验，无需吹气", 12, 208, 216);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *hint = ui_text(ui_root, "未采集真实数据", 12, 236, 216);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_SUBTLE), 0);
    ui_button("结束演示", 12, 264, 216, false, BREATHON_HOME);
    start_tick = lv_tick_get();
    collect_timer = lv_timer_create(collect_tick, 100, NULL);
}
