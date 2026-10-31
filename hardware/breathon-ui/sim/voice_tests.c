#include "breathon_ui.h"
#include "capture.h"
#include <assert.h>
#include <stdio.h>

/* Command counters only: never access microphone, network, or a paid API. */
static unsigned starts;
static unsigned stops;
static unsigned replacement_starts;
static unsigned replacement_stops;
static bool publish_stop;

static void voice_sink(bool start)
{
    assert(breathon_ui_page() == BREATHON_VOICE);
    if (start) ++starts;
    else {
        ++stops;
        if (publish_stop) breathon_ui_set_voice("待命", NULL);
    }
}

static void replacement_sink(bool start)
{
    assert(breathon_ui_page() == BREATHON_VOICE);
    if (start) ++replacement_starts;
    else ++replacement_stops;
}

static void voice_tap(void) { sim_tap(breathon_ui_page() == BREATHON_HOME ? 64 : 180, breathon_ui_page() == BREATHON_HOME ? 230 : 276); }

void sim_voice_demo_tests(void)
{
    breathon_ui_show(BREATHON_HOME);
    breathon_ui_set_voice_handler(voice_sink);
    unsigned live_starts = starts;
    breathon_ui_enable_voice_demo();
    unsigned idle_timers = sim_timer_count();
    sim_capture("67-demo-home");
    assert(sim_label(lv_screen_active(), "本地演示"));
    breathon_ui_show(BREATHON_VOICE);
    sim_advance(7000);
    assert(!sim_label(lv_screen_active(), "你好，我是小息。\n今天也轻松一点。"));
    breathon_ui_show(BREATHON_HOME);
    sim_tap(64,230);
    assert(sim_label(lv_screen_active(), "通话演示"));
    assert(!sim_label(lv_screen_active(), "静音"));
    assert(sim_label(lv_screen_active(), "你好，我是小息。\n今天也轻松一点。"));
    sim_capture("68-demo-call");
    sim_tap(64,286);
    assert(sim_label(lv_screen_active(), "坐久了，活动一下。\n按自己的节奏慢慢来。"));
    sim_capture("69-demo-next");
    sim_advance(6100);
    assert(sim_label(lv_screen_active(), "记得适量补水。\n也给自己一点休息时间。"));
    sim_capture("70-demo-auto");
    sim_tap(64,286);
    sim_capture("71-demo-reminder");
    sim_tap(176,286);
    assert(breathon_ui_page()==BREATHON_HOME);
    assert(sim_timer_count()==idle_timers);
    for (unsigned i=0;i<50;++i) {
        sim_tap(64,230);
        breathon_ui_show(BREATHON_HOME);
        assert(sim_timer_count()==idle_timers);
    }
    sim_advance(7000);
    assert(sim_label(lv_screen_active(), "本地演示"));
    assert(starts==live_starts);
    breathon_ui_show(BREATHON_DEVICE);
    sim_capture("72-demo-device");
    breathon_ui_show(BREATHON_HOME);
    puts("PASS: demo call never starts live sink; next/auto/end/reenter; 50 exits no timer leak");
}

void sim_voice_tests(void)
{
    /* Given no handler, when touched, then HOME remains truthful and disabled. */
    voice_tap();
    assert(breathon_ui_page() == BREATHON_HOME);
    assert(starts == 0 && stops == 0);


    /* Given registration, when clicking once, then emit exactly one command. */
    breathon_ui_set_voice_handler(voice_sink);
    assert(starts == 0 && stops == 0);
    breathon_ui_set_voice("初始化", "真实转写保留");
    sim_press(64, 230, true);
    sim_advance(1200); /* Holding the entry is not permission for repeated starts. */
    assert(starts == 0);
    sim_press(64, 230, false);
    assert(breathon_ui_page() == BREATHON_VOICE);
    assert(starts == 1 && stops == 0);
    assert(sim_label(lv_screen_active(), "初始化"));
    assert(sim_label(lv_screen_active(), "真实转写保留"));
    sim_capture("34-voice-start-pending");

    /* Given an active request, status/reconnect/refresh never start or stop. */
    for (int i = 0; i < 30; ++i) {
        breathon_ui_set_voice("连接中", NULL);
        breathon_ui_set_network(true, i % 2 == 0);
        breathon_ui_show(BREATHON_VOICE);
        breathon_ui_set_voice_handler(voice_sink);
        sim_advance(80);
    }
    assert(starts == 1 && stops == 0);
    sim_capture("41-voice-reconnect");
    breathon_ui_set_voice("连接失败", NULL);
    assert(!sim_label(lv_screen_active(), "待命"));
    sim_capture("35-voice-error");

    /* Without external STOP feedback the UI must keep the last published state. */
    voice_tap();
    assert(breathon_ui_page() == BREATHON_HOME);
    assert(starts == 1 && stops == 1);
    assert(sim_label(lv_screen_active(), "连接失败"));
    sim_capture("36-home-stop-pending");
    breathon_ui_show(BREATHON_HOME);
    assert(stops == 1);

    /* Synchronous firmware feedback is safe before leaving VOICE. */
    publish_stop = true;
    voice_tap();
    breathon_ui_set_voice("聆听中", NULL);
    voice_tap();
    assert(starts == 2 && stops == 2);
    assert(sim_label(lv_screen_active(), "语音陪伴"));
    sim_capture("37-home-external-idle");

    /* Removal closes the original owner once; later navigation cannot repeat it. */
    voice_tap();
    breathon_ui_set_voice_handler(NULL);
    assert(starts == 3 && stops == 3);
    sim_capture("38-voice-handler-removed");
    breathon_ui_set_voice_handler(NULL);
    voice_tap();
    voice_tap();
    assert(breathon_ui_page() == BREATHON_HOME);
    assert(starts == 3 && stops == 3);
    sim_capture("39-home-handler-removed");

    /* Replacement stops the old owner without starting or stopping the new one. */
    breathon_ui_set_voice_handler(voice_sink);
    voice_tap();
    breathon_ui_set_voice_handler(replacement_sink);
    assert(starts == 4 && stops == 4);
    assert(replacement_starts == 0 && replacement_stops == 0);
    voice_tap();
    voice_tap();
    assert(replacement_starts == 1 && replacement_stops == 0);
    voice_tap();
    assert(replacement_stops == 1);

    /* Each valid exit stops BEFORE navigation; same/invalid pages do nothing. */
    breathon_ui_set_voice_handler(voice_sink);
    for (int page = BREATHON_HOME; page <= BREATHON_SUGGESTIONS; ++page) {
        if (page == BREATHON_VOICE) continue;
        breathon_ui_show(BREATHON_HOME);
        voice_tap();
        unsigned start_count = starts;
        unsigned stop_count = stops;
        breathon_ui_show(BREATHON_VOICE);
        breathon_ui_show((breathon_page_t)99);
        assert(starts == start_count && stops == stop_count);
        breathon_ui_show((breathon_page_t)page);
        assert(starts == start_count && stops == stop_count + 1);
    }

    /* A programmatic page preview is not authorization for a live session. */
    breathon_ui_show(BREATHON_HOME);
    unsigned start_count = starts;
    unsigned stop_count = stops;
    breathon_ui_set_voice("初始化", NULL);
    breathon_ui_show(BREATHON_VOICE);
    assert(sim_label(lv_screen_active(), "真实转写保留"));
    sim_capture("40-voice-programmatic");
    voice_tap();
    assert(starts == start_count && stops == stop_count);
    breathon_ui_set_voice_handler(NULL);
    puts("PASS: native pointer voice start/stop exactly once; 7 exits stop before navigation");
    puts("PASS: hold/refresh/status/reconnect/register never start; remove/replace owner safely");
    puts("PASS: initial/error/stop status stays external; synchronous idle callback and transcript retained");
}

