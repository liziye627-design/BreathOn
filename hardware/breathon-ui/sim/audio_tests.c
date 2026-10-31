#include "breathon_ui.h"
#include "capture.h"
#include "../src/ui_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* QA-only sink. It never opens a microphone or produces sound. */
static unsigned calls[4];
static void qa_handler(breathon_audio_action_t action)
{
    assert(breathon_ui_page() == BREATHON_AUDIO);
    ++calls[action];
    if (action == BREATHON_AUDIO_STOP) {
        breathon_audio_status_t stopped = {"QA 已停止", 0, 0, true, false};
        breathon_ui_set_audio(&stopped); /* Synchronous STOP feedback must be safe. */
    }
}

static void publish(const char *state, uint32_t ms, uint8_t level, bool recorded, bool busy)
{
    breathon_audio_status_t status = {state, ms, level, recorded, busy};
    breathon_ui_set_audio(&status);
    sim_verify();
}

static void button_enabled(const char *text, bool enabled)
{
    lv_obj_t *label = sim_label(lv_screen_active(), text);
    assert(label);
    lv_obj_t *button = lv_obj_get_parent(label);
    assert(lv_obj_has_state(button, LV_STATE_DISABLED) != enabled);
    uint32_t background = enabled ? (!strcmp(text, "录音5秒") ? UI_BUTTON : UI_WHITE) : UI_PALE;
    assert(lv_color_eq(lv_obj_get_style_bg_color(button, LV_PART_MAIN), lv_color_hex(background)));
}

static void all_actions(void)
{
    sim_tap(64, 182);
    sim_tap(176, 182);
    sim_tap(64, 234);
    sim_tap(176, 234);
}

void sim_audio_tests(void)
{
    /* Hidden maintenance surface: enter through API, never a product button. */
    breathon_ui_set_voice("思考中", "离线测试前的识别文字");
    sim_tap(64, 230);
    assert(breathon_ui_page() == BREATHON_VOICE);
    breathon_ui_show(BREATHON_AUDIO);
    assert(breathon_ui_page() == BREATHON_AUDIO);
    sim_capture("17-audio-unavailable");
    all_actions();
    for (int i = 0; i < 4; ++i) assert(calls[i] == 0);
    button_enabled("录音5秒", false);
    button_enabled("回放", false);
    button_enabled("停止", false);
    button_enabled("测试音", false);
    publish("QA 正在录音", 2500, 88, true, true);
    assert(sim_label(lv_screen_active(), "音频未接入"));
    assert(sim_label(lv_screen_active(), "电平 0%"));
    sim_tap(120, 286);
    assert(breathon_ui_page() == BREATHON_VOICE);
    assert(sim_label(lv_screen_active(), "思考中"));
    assert(sim_label(lv_screen_active(), "离线测试前的识别文字"));
    assert(calls[BREATHON_AUDIO_STOP] == 0);
    puts("PASS: NULL handler disables actions, hides stale levels; voice state/text preserved");

    breathon_ui_set_audio_handler(qa_handler);
    publish("QA 待命", 0, 0, false, false);
    breathon_ui_show(BREATHON_AUDIO);
    sim_capture("18-audio-idle");
    button_enabled("录音5秒", true);
    button_enabled("测试音", true);
    button_enabled("回放", false);
    button_enabled("停止", false);
    sim_tap(176, 182);
    assert(calls[BREATHON_AUDIO_PLAY] == 0);
    sim_press(64, 182, true);
    sim_capture("19-audio-record-pressed");
    sim_press(64, 182, false);
    assert(calls[BREATHON_AUDIO_RECORD] == 1);
    /* A click is a command, not evidence of capture; state waits for the caller. */
    assert(sim_label(lv_screen_active(), "QA 待命"));
    publish("QA 准备录音", 0, 0, false, true);
    sim_capture("20-audio-preparing");
    publish("QA 正在录音", 2500, 64, false, true);
    sim_capture("21-audio-recording");
    button_enabled("录音5秒", false);
    button_enabled("回放", false);
    button_enabled("测试音", false);
    button_enabled("停止", true);
    sim_tap(64, 182);
    sim_tap(176, 182);
    sim_tap(176, 234);
    assert(calls[BREATHON_AUDIO_RECORD] == 1 && calls[BREATHON_AUDIO_PLAY] == 0 && calls[BREATHON_AUDIO_TONE] == 0);
    unsigned stop_count = calls[BREATHON_AUDIO_STOP];
    lv_obj_t *stable_label = sim_label(lv_screen_active(), "QA 正在录音");
    for (int i = 0; i < 30; ++i) {
        publish("QA 正在录音", 2500 + (uint32_t)i * 100, (uint8_t)(i * 3), false, true);
        sim_advance(100);
    }
    assert(sim_label(lv_screen_active(), "QA 正在录音") == stable_label);
    assert(calls[BREATHON_AUDIO_STOP] == stop_count);
    sim_advance(6000);
    assert(breathon_ui_page() == BREATHON_AUDIO);
    assert(sim_label(lv_screen_active(), "QA 正在录音"));
    assert(sim_label(lv_screen_active(), "5.4 秒"));
    puts("PASS: 100ms updates in place, no automatic capture/timing/STOP, busy action guards");

    publish("QA 录音完成", 5000, 0, true, false);
    sim_capture("22-audio-ready");
    button_enabled("回放", true);
    sim_tap(176, 182);
    assert(calls[BREATHON_AUDIO_PLAY] == 1);
    publish("QA 正在回放", 1400, 43, true, true);
    button_enabled("录音5秒", false);
    button_enabled("回放", false);
    button_enabled("测试音", false);
    button_enabled("停止", true);
    sim_capture("23-audio-playback");
    sim_tap(64, 234);
    assert(calls[BREATHON_AUDIO_STOP] == stop_count + 1);
    sim_capture("24-audio-stopped");
    sim_tap(176, 234);
    assert(calls[BREATHON_AUDIO_TONE] == 1);
    publish("QA 测试音", 600, 72, true, true);
    sim_capture("25-audio-tone");
    sim_tap(120, 286);
    assert(breathon_ui_page() == BREATHON_VOICE);
    assert(calls[BREATHON_AUDIO_STOP] == stop_count + 2);
    assert(sim_label(lv_screen_active(), "思考中"));
    assert(sim_label(lv_screen_active(), "离线测试前的识别文字"));
    sim_capture("26-voice-returned");
    puts("PASS: four exact action dispatches; STOP before navigation with synchronous status callback");

    breathon_ui_show(BREATHON_AUDIO);
    publish("QA 录音失败", 0, 0, false, false);
    sim_capture("27-audio-error");
    char state[] = "QA 状态已复制";
    publish(state, 1234, 255, true, false);
    state[0] = 'X';
    assert(sim_label(lv_screen_active(), "QA 状态已复制"));
    assert(sim_label(lv_screen_active(), "电平 100%"));
    sim_capture("28-audio-level-clamped");
    char long_state[200] = "QA ";
    for (int i = 0; i < 15; ++i) strcat(long_state, "中文长句");
    publish(long_state, UINT32_MAX, 100, true, false);
    sim_capture("29-audio-long-status");
    publish(NULL, 0, 0, false, false);
    assert(sim_label(lv_screen_active(), "待命"));
    publish(NULL, 0, 0, false, true);
    assert(sim_label(lv_screen_active(), "处理中"));
    breathon_ui_set_audio(NULL);
    button_enabled("回放", false);
    publish("QA 正在录音", 500, 50, false, true);
    breathon_ui_set_audio_handler(NULL);
    sim_capture("30-audio-handler-removed");
    stop_count = calls[BREATHON_AUDIO_STOP];
    all_actions();
    sim_tap(120, 286);
    assert(calls[BREATHON_AUDIO_STOP] == stop_count);
    puts("PASS: copied UTF-8 state, NULL reset/default, uint32 elapsed, level clamp, handler removal");

    breathon_ui_set_audio_handler(qa_handler);
    /* Every valid exit, including direct navigation, dispatches exactly once. */
    for (int page = BREATHON_HOME; page < BREATHON_AUDIO; ++page) {
        breathon_ui_show(BREATHON_AUDIO);
        stop_count = calls[BREATHON_AUDIO_STOP];
        breathon_ui_show(BREATHON_AUDIO);
        breathon_ui_show((breathon_page_t)99);
        assert(calls[BREATHON_AUDIO_STOP] == stop_count);
        breathon_ui_show((breathon_page_t)page);
        assert(calls[BREATHON_AUDIO_STOP] == stop_count + 1);
    }
    breathon_ui_show(BREATHON_HOME);
    breathon_ui_set_audio_handler(NULL);
    puts("PASS: all 7 exit routes STOP once; same-page and invalid navigation do not STOP");
}

