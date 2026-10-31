#include "ui_internal.h"
#include <stdint.h>

static breathon_audio_handler_t audio_handler;
static char state_text[96] = "待命";
static breathon_audio_status_t audio_status;
static lv_obj_t *state_label;
static lv_obj_t *elapsed_label;
static lv_obj_t *level_label;
static lv_obj_t *actions[4];
static breathon_audio_action_t requested_action = BREATHON_AUDIO_RECORD;

static bool action_available(breathon_audio_action_t action)
{
    if (!audio_handler) return false;
    switch (action) {
    case BREATHON_AUDIO_RECORD:
    case BREATHON_AUDIO_TONE: return !audio_status.busy;
    case BREATHON_AUDIO_PLAY: return !audio_status.busy && audio_status.has_recording;
    case BREATHON_AUDIO_STOP: return audio_status.busy;
    }
    return false;
}

static void audio_action(lv_event_t *event)
{
    breathon_audio_action_t action = (breathon_audio_action_t)(intptr_t)lv_event_get_user_data(event);
    if (action_available(action)) {
        requested_action = action;
        audio_handler(action);
    }
}

static void update_audio(void)
{
    if (!ui_root || breathon_ui_page() != BREATHON_AUDIO) return;
    uint32_t elapsed = audio_handler ? audio_status.elapsed_ms : 0;
    uint8_t level = audio_handler ? audio_status.level : 0;
    lv_label_set_text(state_label, audio_handler ? state_text : "音频未接入");
    lv_label_set_text_fmt(elapsed_label, "%lu.%lu 秒",
                         (unsigned long)(elapsed / 1000), (unsigned long)(elapsed % 1000 / 100));
    lv_label_set_text_fmt(level_label, "电平 %u%%", (unsigned)level);
    ui_motion_level(level, audio_status.busy, audio_handler != NULL, requested_action);
    for (int i = 0; i < 4; ++i) {
        if (action_available((breathon_audio_action_t)i)) lv_obj_remove_state(actions[i], LV_STATE_DISABLED);
        else lv_obj_add_state(actions[i], LV_STATE_DISABLED);
    }
}

void breathon_ui_set_audio_handler(breathon_audio_handler_t handler)
{
    audio_handler = handler;
    update_audio();
}

void breathon_ui_set_audio(const breathon_audio_status_t *status)
{
    if (status) {
        audio_status = *status;
        if (audio_status.level > 100) audio_status.level = 100;
        ui_copy_text(state_text, sizeof(state_text), status->state ? status->state :
                     (status->busy ? "处理中" : "待命"));
    } else {
        audio_status = (breathon_audio_status_t){0};
        ui_copy_text(state_text, sizeof(state_text), "待命");
    }
    /* Do not retain a caller-owned string, even while this page is hidden. */
    audio_status.state = state_text;
    update_audio();
}

void ui_audio_leave(void)
{
    if (audio_handler) audio_handler(BREATHON_AUDIO_STOP);
}

void ui_audio(void)
{
    ui_header("离线录音");
    lv_obj_t *hint = ui_text(ui_root, "无需联网，断电清除", 12, 40, 216);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_SUBTLE), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *panel = ui_panel(ui_root, 12, 68, 216, 84, UI_WHITE);
    state_label = ui_text(panel, "", 8, 4, 200);
    lv_obj_set_style_text_align(state_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_height(state_label, 24);
    lv_label_set_long_mode(state_label, LV_LABEL_LONG_DOT);
    ui_motion_wave(panel);
    level_label = ui_text(panel, "", 8, 56, 100);
    elapsed_label = ui_text(panel, "", 108, 56, 100);
    lv_obj_set_style_text_align(elapsed_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_height(elapsed_label, 24);
    lv_label_set_long_mode(elapsed_label, LV_LABEL_LONG_DOT);
    actions[BREATHON_AUDIO_RECORD] = ui_action_button("录音5秒", 12, 160, 104, true,
                                                    audio_action, (void *)(intptr_t)BREATHON_AUDIO_RECORD);
    actions[BREATHON_AUDIO_PLAY] = ui_action_button("回放", 124, 160, 104, false,
                                                  audio_action, (void *)(intptr_t)BREATHON_AUDIO_PLAY);
    actions[BREATHON_AUDIO_STOP] = ui_action_button("停止", 12, 212, 104, false,
                                                  audio_action, (void *)(intptr_t)BREATHON_AUDIO_STOP);
    actions[BREATHON_AUDIO_TONE] = ui_action_button("测试音", 124, 212, 104, false,
                                                  audio_action, (void *)(intptr_t)BREATHON_AUDIO_TONE);
    for (int i = 0; i < 4; ++i) ui_soft_button(actions[i], i == BREATHON_AUDIO_RECORD);
    ui_soft_button(ui_button("返回语音", 12, 264, 216, false, BREATHON_VOICE), false);
    update_audio();
}
