#ifndef BREATHON_UI_INTERNAL_H
#define BREATHON_UI_INTERNAL_H
#include "breathon_ui.h"
#include "ui_timeline.h"

#define UI_INK 0x083F49
#define UI_CYAN 0x10C8C3
#define UI_PALE 0xD9ECEF
#define UI_BG 0x073D48
#define UI_BG_END 0x062F39
#define UI_TEXT 0xF0FCFC
#define UI_SUBTLE 0xB6DADD
#define UI_WHITE 0xF2FBFA
#define UI_MUTED 0x4E727A
#define UI_BUTTON UI_CYAN
#define UI_BUTTON_DARK 0x00A8AD
#define UI_PRESS_INK 0x042B34
#define UI_BORDER 0x9DD7DA
#define UI_WASH 0x0B5660
#define UI_LIGHT_CYAN UI_CYAN
#define UI_LIGHT_BLUE UI_BUTTON
#define UI_LIGHT_WASH UI_WASH
#define UI_ORANGE 0xFF4A1F
#define UI_ORANGE_LIGHT 0xFF8A54
#define UI_BLUSH 0xFFD3A9
#define UI_ROSE 0xFFF0E5
#define UI_RED 0xF35C60
#define UI_CARD_END 0xE7F6F6
#define UI_TEAL 0x00BDB8
#define UI_ICON_WASH 0xD9FAF7
#define UI_PLOT_GAS 0x00858A
#define UI_PLOT_HEAT 0xC94318
LV_FONT_DECLARE(font_puhui_14_1);
#ifndef BREATHON_UI_REDUCED_MOTION
#define BREATHON_UI_REDUCED_MOTION 0
#endif

extern lv_obj_t *ui_root;
typedef struct { lv_obj_t *eyes[2]; lv_obj_t *mouth; } ui_face_t;
ui_face_t ui_face(lv_obj_t *parent, int size);
lv_obj_t *ui_text(lv_obj_t *parent, const char *text, int x, int y, int w);
lv_obj_t *ui_panel(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color);
lv_obj_t *ui_button(const char *text, int x, int y, int w, bool primary, breathon_page_t target);
lv_obj_t *ui_action_button(const char *text, int x, int y, int w, bool primary,
                           lv_event_cb_t callback, void *data);
void ui_copy_text(char *output, size_t size, const char *input);
void ui_audio(void);
void ui_audio_leave(void);
void ui_soft_button(lv_obj_t *button, bool primary);
void ui_motion_orb(int y, bool quiet);
void ui_rotom_create(lv_obj_t *parent);
void ui_rotom_frame(uint32_t elapsed_ms, ui_mood_t mood);
void ui_motion_voice_state(const char *state);
void ui_motion_wave(lv_obj_t *parent);
void ui_motion_level(uint8_t level, bool busy, bool available, breathon_audio_action_t action);
void ui_motion_cancel(void);
void ui_header(const char *title);
void ui_orb(int x, int y, int size);
void ui_home(const char *state);
void ui_voice(const char *state, const char *transcript);
void ui_voice_update(const char *state, const char *transcript);
void ui_voice_start(lv_event_t *event);
void ui_voice_control_update(bool available);
void ui_network_update(const char *network);
void ui_prepare(void);
void ui_collect(void);
void ui_result(void);
void ui_history(void);
void ui_device(const char *network);
void ui_collect_cancel(void);
void ui_dashboard(const char *state);
void ui_activity(void);
void ui_reminder(void);
void ui_reminder_refresh(void);
void ui_reminder_tick(void);
void ui_dashboard_voice(const char *state, bool available);
void ui_pages_reset(void);
void ui_call_mute_update(bool available, bool muted);
void ui_mute_toggle(lv_event_t *event);
void ui_small(lv_obj_t *label);
void ui_background(void);
void ui_nav_header(const char *title, breathon_page_t back);
void ui_icon(lv_obj_t *parent, const char *kind, int x, int y, uint32_t color);
lv_obj_t *ui_nav_card(int y, int h, breathon_page_t target);
const char *ui_network_text(void);
bool ui_mute_available(void);
bool ui_is_muted(void);
const char *ui_current_voice(void);
void ui_gas(void);
void ui_relation(void);
void ui_insight(void);
void ui_suggestions(void);
void ui_suggestions_reset(void);
void ui_suggestions_reminder(void);
const char *ui_reminder_summary(void);
void ui_demo_toggle(lv_event_t *event);
void ui_reminder_choose(int kind);
void ui_metric_badge(lv_obj_t *parent, int x, int y);
void ui_metric_number(lv_obj_t *parent, const char *value, int x, int y, int width);
void ui_chart_lines(lv_obj_t *parent, int period, bool gas_only);
const char *ui_demo_note(void);
#endif
