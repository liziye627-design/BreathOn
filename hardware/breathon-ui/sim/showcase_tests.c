#include "breathon_ui.h"
#include "capture.h"
#include <assert.h>

void sim_showcase_tests(void)
{
    /* Given the explicitly labelled demo, all four home cards navigate. */
    breathon_ui_set_demo(true);
    breathon_ui_show(BREATHON_HOME);
    assert(sim_label(lv_screen_active(),"模拟数据 · 仅供展示"));
    sim_tap(176,230); assert(breathon_ui_page()==BREATHON_SUGGESTIONS);
    sim_tap(22,22); assert(breathon_ui_page()==BREATHON_HOME);
    sim_tap(64,126);
    assert(breathon_ui_page()==BREATHON_GAS);
    assert(sim_label(lv_screen_active(),"1.26"));
    sim_capture("51-gas-demo");
    sim_tap(120,274);
    assert(breathon_ui_page()==BREATHON_RELATION);
    sim_capture("52-relation-day");
    sim_tap(120,70); assert(sim_label(lv_screen_active(),"最近7天"));
    sim_capture("53-relation-week");
    sim_tap(194,70); assert(sim_label(lv_screen_active(),"最近30天"));
    sim_capture("54-relation-month");
    sim_tap(120,286);
    assert(breathon_ui_page()==BREATHON_INSIGHT);
    sim_capture("55-insight-demo");
    sim_tap(120,286);
    assert(breathon_ui_page()==BREATHON_SUGGESTIONS);
    sim_capture("56-suggestions");
    sim_tap(120,76);
    assert(breathon_ui_page()==BREATHON_REMINDER);
    assert(sim_label(lv_screen_active(),"补水提醒"));
    sim_capture("57-water-reminder");
    sim_tap(60,292);
    assert(sim_label(lv_screen_active(),"10分后补水"));
    breathon_ui_show(BREATHON_SUGGESTIONS); sim_tap(120,200);
    assert(sim_label(lv_screen_active(),"休息提醒")); sim_capture("63-rest-reminder");
    sim_advance(2000);
    assert(sim_label(lv_screen_active(),"放松片刻，缓一缓"));
    assert(!sim_label(lv_screen_active(),"10分后补水"));
    sim_advance(600000);
    assert(breathon_ui_page()==BREATHON_REMINDER);
    assert(sim_label(lv_screen_active(),"休息提醒"));
    assert(sim_label(lv_screen_active(),"放松片刻，缓一缓"));
    sim_capture("65-rest-selection-preserved");
    sim_tap(180,292); assert(sim_label(lv_screen_active(),"该喝水啦"));
    breathon_ui_show(BREATHON_SUGGESTIONS); sim_tap(120,266);
    assert(sim_label(lv_screen_active(),"该喝水啦"));
    sim_tap(60,292); /* Snooze the active water reminder. */
    breathon_ui_show(BREATHON_SUGGESTIONS);
    sim_advance(61000);
    assert(breathon_ui_page()==BREATHON_SUGGESTIONS);
    assert(sim_label(lv_screen_active(),"9分后补水"));
    sim_advance(540000);
    assert(breathon_ui_page()==BREATHON_SUGGESTIONS);
    assert(sim_label(lv_screen_active(),"该喝水啦"));
    sim_capture("66-suggestions-reminder-due");
    sim_tap(120,266); sim_tap(180,292);

    /* Disabling demo removes synthetic metrics from every product data page. */
    breathon_ui_show(BREATHON_DEVICE); sim_tap(176,226);
    assert(!breathon_ui_demo_enabled()); sim_capture("64-device-demo-off");
    breathon_ui_show(BREATHON_HOME);
    assert(!sim_label(lv_screen_active(),"1.26"));
    sim_capture("58-home-live-empty");
    breathon_ui_show(BREATHON_GAS); sim_capture("59-gas-empty");
    assert(!sim_label(lv_screen_active(),"1.26"));
    breathon_ui_show(BREATHON_ACTIVITY); sim_capture("60-calories-empty");
    assert(!sim_label(lv_screen_active(),"326"));
    breathon_ui_show(BREATHON_RELATION); sim_capture("61-relation-empty");
    assert(sim_label(lv_screen_active(),"暂无数据"));
    breathon_ui_show(BREATHON_INSIGHT); sim_capture("62-insight-empty");
    assert(sim_label(lv_screen_active(),"等待真实数据"));
    breathon_ui_show(BREATHON_HISTORY);
    assert(sim_label(lv_screen_active(),"还没有记录"));
    breathon_ui_set_demo(true);
    breathon_ui_show(BREATHON_HOME);
}


