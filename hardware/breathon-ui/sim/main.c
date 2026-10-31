#include "breathon_ui.h"
#include "capture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(font_puhui_18_2);
void sim_showcase_tests(void);
static unsigned starts,stops,mutes;
static bool gate,accept=true;
static void voice(bool start) { if(start) ++starts; else ++stops; }
static bool mute(bool muted) { ++mutes; if(accept) gate=muted; return accept; }

static lv_obj_t *transcript_view(void)
{
    lv_obj_t *root=lv_screen_active();
    for(uint32_t i=0;i<lv_obj_get_child_count(root);++i) {
        lv_obj_t *child=lv_obj_get_child(root,(int32_t)i);
        for(uint32_t j=0;j<lv_obj_get_child_count(child);++j) {
            lv_obj_t *nested=lv_obj_get_child(child,(int32_t)j);
            if(lv_obj_has_flag(nested,LV_OBJ_FLAG_SCROLLABLE) && lv_obj_get_scroll_dir(nested)==LV_DIR_VER) return nested;
        }
    }
    return NULL;
}

int main(void)
{
    sim_init();
    breathon_ui_set_audio(NULL); breathon_ui_set_audio_handler(NULL);
    breathon_ui_set_voice_handler(voice);
    breathon_ui_init(&font_puhui_18_2);
    assert(starts==0);
    breathon_ui_set_voice_handler(NULL);
    sim_capture("01-home-unknown");
    assert(sim_label(lv_screen_active(),"语音暂未接入"));
    sim_voice_tests();
    breathon_ui_set_voice_handler(voice);
    breathon_ui_set_mute_handler(mute);
    breathon_ui_set_voice("待命","");
    breathon_ui_set_network(true,true);
    sim_capture("02-home-connected");
    assert(sim_label(lv_screen_active(),"今日活动消耗"));
    assert(sim_label(lv_screen_active(),"和小息通话"));
    assert(sim_label(lv_screen_active(),"下一个提醒"));
    breathon_ui_set_voice("聆听中","首页保留转写"); sim_capture("32-home-callback");
    breathon_ui_set_voice("当前状态文字较长仍保持单行",NULL); sim_capture("33-home-long-status");
    breathon_ui_set_voice("正在连接","正在连接语音服务");
    sim_press(64,230,true); sim_capture("03-home-pressed"); sim_press(64,230,false);
    assert(breathon_ui_page()==BREATHON_VOICE && starts==1);
    sim_capture("04-voice-idle");
    breathon_ui_set_voice("小息 · 聆听中","请说出你的问题"); sim_capture("05-voice-listening");
    breathon_ui_set_voice("小息 · 思考中","请介绍一下这个设备"); sim_capture("06-voice-thinking");
    breathon_ui_set_voice("小息 · 播放中","你好，我是灵息。"); sim_capture("07-voice-speaking");
    sim_tap(60,276); assert(gate && mutes==1); sim_capture("42-voice-muted");
    accept=false; sim_tap(60,276); assert(gate && mutes==2);
    accept=true; sim_tap(60,276); assert(!gate && mutes==3);
    char long_text[1300]="AB";
    for(int i=0;i<100;++i) strcat(long_text,"中文长句");
    breathon_ui_set_voice("聆听中",long_text); sim_capture("08-voice-long-text");
    lv_obj_t *viewport=transcript_view(); assert(viewport);
    const lv_font_t *font=lv_obj_get_style_text_font(viewport,LV_PART_MAIN);
    assert(lv_obj_get_height(viewport)==3*font->line_height);
    for(int i=0;i<20;++i) {
        sim_press(120,237,true);
        for(int y=217;y>=57;y-=20) sim_press(120,y,true);
        sim_press(120,57,false); sim_advance(400);
    }
    sim_advance(1000);
    assert(lv_obj_get_scroll_y(viewport)>0 && lv_obj_get_scroll_bottom(viewport)<=0);
    assert(lv_obj_get_scroll_y(lv_screen_active())==0);
    sim_capture("31-voice-transcript-end");
    sim_tap(180,276); assert(breathon_ui_page()==BREATHON_HOME && stops==1);
    sim_tap(176,126); assert(breathon_ui_page()==BREATHON_ACTIVITY);
    sim_capture("43-activity-empty"); assert(sim_label(lv_screen_active(),"326"));
    sim_tap(22,22); assert(breathon_ui_page()==BREATHON_HOME);
    breathon_ui_show(BREATHON_REMINDER); assert(breathon_ui_page()==BREATHON_REMINDER);
    sim_capture("44-reminder-empty");
    sim_tap(60,292); assert(breathon_ui_page()==BREATHON_HOME);
    assert(sim_label(lv_screen_active(),"10分后活动")); sim_capture("45-home-reminder-set");
    breathon_ui_show(BREATHON_REMINDER); sim_capture("46-reminder-scheduled");
    sim_advance(60000);
    assert(sim_label(lv_screen_active(),"9分后活动"));
    sim_capture("50-reminder-minute-update");
    sim_tap(180,292); assert(sim_label(lv_screen_active(),"还未设置提醒"));
    breathon_ui_show(BREATHON_REMINDER); sim_tap(60,292); sim_advance(600100);
    assert(breathon_ui_page()==BREATHON_REMINDER); sim_capture("47-reminder-due");
    sim_tap(60,292); assert(sim_label(lv_screen_active(),"10分后活动"));
    sim_tap(64,230); sim_advance(600100);
    assert(breathon_ui_page()==BREATHON_VOICE); /* Due never interrupts a call. */
    sim_tap(180,276); assert(sim_label(lv_screen_active(),"该活动一下啦"));
    breathon_ui_show(BREATHON_REMINDER); sim_tap(180,292);
    assert(sim_label(lv_screen_active(),"本次已完成")); sim_capture("48-home-reminder-done");
    sim_tap(210,22); assert(breathon_ui_page()==BREATHON_DEVICE);
    sim_capture("14-device-connected");
    breathon_ui_set_voice("语音暂不可用","无法连接网关"); sim_capture("49-device-gateway-error");
    breathon_ui_set_network(true,false); sim_capture("15-device-disconnected");
    breathon_ui_set_network(false,false); sim_capture("16-device-unknown");
    sim_tap(64,170); assert(breathon_ui_page()==BREATHON_PREPARE); sim_capture("09-prepare");
    sim_advance(6000); assert(breathon_ui_page()==BREATHON_PREPARE);
    sim_tap(120,286); sim_tap(210,22); sim_tap(64,226);
    assert(breathon_ui_page()==BREATHON_COLLECT); sim_capture("10-collect-start");
    sim_advance(3000); sim_capture("11-collect-mid");
    sim_tap(120,286); sim_advance(6000); assert(breathon_ui_page()==BREATHON_HOME);
    sim_tap(210,22); sim_tap(64,226); sim_advance(5100);
    assert(breathon_ui_page()==BREATHON_RESULT); sim_capture("12-result-empty");
    sim_tap(120,286); sim_tap(210,22); sim_tap(176,170);
    assert(breathon_ui_page()==BREATHON_HISTORY); sim_capture("13-history-empty");
    sim_tap(120,286);
    sim_audio_tests(); sim_motion_tests(); sim_showcase_tests();
    extern void sim_voice_demo_tests(void);
    sim_voice_demo_tests();
    printf("PASS: real pointer cards/call/mute/reminder/navigation; %u glyph/geometry checks\n",sim_checks);
    puts("Native simulator does not prove radio, physical touch or microphone capture.");
    return 0;
}


