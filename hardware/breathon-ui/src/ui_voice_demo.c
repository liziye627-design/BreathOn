#include "ui_internal.h"

static bool enabled;
static lv_timer_t *script_timer;
static unsigned phrase;
static const char *const phrases[] = {
    "你好，我是小息。\n今天也轻松一点。",
    "坐久了，活动一下。\n按自己的节奏慢慢来。",
    "记得适量补水。\n也给自己一点休息时间。",
    "按既定计划用药，\n照顾好每天的自己。"
};

bool breathon_ui_voice_demo_enabled(void) { return enabled; }

void breathon_ui_voice_demo_next(lv_event_t *event)
{
    (void)event;
    if (!enabled || !script_timer || breathon_ui_page() != BREATHON_VOICE) return;
    phrase = (phrase + 1) % (sizeof(phrases) / sizeof(phrases[0]));
    breathon_ui_set_voice("小息 · 说话演示", phrases[phrase]);
    lv_timer_reset(script_timer);
}

static void script_tick(lv_timer_t *timer)
{
    (void)timer;
    breathon_ui_voice_demo_next(NULL);
}

static void demo_action(bool start)
{
    if (script_timer) {
        lv_timer_delete(script_timer);
        script_timer = NULL;
    }
    phrase = 0;
    if (!start) {
        breathon_ui_set_voice("本地演示", "");
        return;
    }
    script_timer = lv_timer_create(script_tick, 6000, NULL);
    breathon_ui_set_voice("小息 · 说话演示", phrases[0]);
}

void breathon_ui_enable_voice_demo(void)
{
    enabled = true;
    breathon_ui_set_voice_handler(demo_action);
    breathon_ui_set_mute_handler(NULL);
    breathon_ui_set_voice("本地演示", "");
}
