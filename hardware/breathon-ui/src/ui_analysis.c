#include "ui_internal.h"
#include <stdint.h>

static int period;
static lv_obj_t *next_reminder;
void ui_suggestions_reset(void) { next_reminder=NULL; }
void ui_suggestions_reminder(void)
{
    if(next_reminder) lv_label_set_text(next_reminder,ui_reminder_summary());
}
const char *ui_chart_correlation(int period);
const char *ui_reminder_summary(void);
void ui_reminder_current(lv_event_t *event);
static lv_obj_t *small(lv_obj_t *p,const char *s,int x,int y,int w)
{ lv_obj_t *l=ui_text(p,s,x,y,w);ui_small(l);return l; }
static void period_click(lv_event_t *event)
{ period=(int)(intptr_t)lv_event_get_user_data(event); breathon_ui_show(BREATHON_RELATION); }
void ui_relation(void)
{
    ui_nav_header("气体 × 热量",BREATHON_GAS);
    const char *tabs[]={"日","周","月"};
    for(int i=0;i<3;++i) {
        lv_obj_t *tab=ui_action_button(tabs[i],12+i*74,47,68,i==period,period_click,(void *)(intptr_t)i);
        if(i!=period) lv_obj_set_style_bg_color(tab,lv_color_hex(UI_PALE),0);
    }
    lv_obj_t *chart=ui_panel(ui_root,12,99,216,112,UI_WHITE);
    if(breathon_ui_demo_enabled()) {
        lv_obj_t *gas=ui_panel(chart,10,13,7,7,UI_PLOT_GAS);
        lv_obj_t *heat=ui_panel(chart,74,13,7,7,UI_PLOT_HEAT);
        lv_obj_set_style_radius(gas,LV_RADIUS_CIRCLE,0); lv_obj_set_style_radius(heat,LV_RADIUS_CIRCLE,0);
        small(chart,"气体",22,7,42); small(chart,"热量",86,7,42);
        small(chart,"模拟",166,7,40);
        lv_obj_t *plot=ui_panel(chart,10,28,196,68,UI_WHITE);
        lv_obj_set_style_radius(plot,0,0); ui_chart_lines(plot,period,false);
        const char *ranges[]={"今日样例","最近7天","最近30天"};
        lv_obj_t *range=small(chart,ranges[period],10,94,196);
        lv_obj_set_style_text_align(range,LV_TEXT_ALIGN_CENTER,0);
    } else { small(chart,"暂无数据",10,7,196); small(chart,"接入真实数据后再分析",10,52,196); }
    const char *labels[]={"样例相关系数","关系解读"};
    const char *values[]={breathon_ui_demo_enabled()?ui_chart_correlation(period):"--",breathon_ui_demo_enabled()?"同向变化":"待接入"};
    for(int i=0;i<2;++i) {
        lv_obj_t *tile=ui_panel(ui_root,12+i*112,219,104,43,UI_WHITE);
        small(tile,labels[i],8,3,88); ui_text(tile,values[i],8,22,88);
    }
    ui_button("查看小息解读  ›",12,270,216,true,BREATHON_INSIGHT);
}
void ui_insight(void)
{
    ui_nav_header("小息解读",BREATHON_RELATION);
    ui_motion_orb(42,true);
    lv_obj_t *card=ui_panel(ui_root,12,166,216,93,UI_WHITE);
    small(card,breathon_ui_demo_enabled()?"样例解读":"等待真实数据",12,8,192);
    small(card,breathon_ui_demo_enabled()?"这组模拟样例中，气体特征\n与活动估算同向变化。\n样例不代表你的身体状态。":"连接数据源后，\n在这里查看记录与趋势。",12,34,192);
    ui_button("建议与提醒  ›",12,270,216,true,BREATHON_SUGGESTIONS);
}
static void choose(lv_event_t *event)
{ ui_reminder_choose((int)(intptr_t)lv_event_get_user_data(event)); }
void ui_suggestions(void)
{
    ui_nav_header("建议与提醒",BREATHON_HOME);
    const char *titles[]={"补水提醒","适当活动","放松休息"};
    const char *notes[]={"适量饮水，照顾自己","起身走走，量力而行","放松一下，缓一缓"};
    const char *icons[]={"water","walk","moon"};
    for(int i=0;i<3;++i) {
        lv_obj_t *row=ui_action_button("",12,48+i*62,216,false,choose,(void *)(intptr_t)i);
        lv_obj_set_height(row,54); lv_obj_set_style_border_width(row,0,0);
        ui_icon(row,icons[i],10,11,i==1?UI_TEAL:UI_CYAN);
        small(row,titles[i],52,7,152);
        lv_obj_t *hint=small(row,notes[i],52,30,154);
        lv_obj_set_style_text_color(hint,lv_color_hex(UI_MUTED),0);
    }
    lv_obj_t *next=ui_action_button("",12,238,216,false,ui_reminder_current,NULL);
    lv_obj_set_height(next,58); lv_obj_set_style_border_width(next,0,0);
    ui_icon(next,"calendar",10,13,UI_ORANGE);
    small(next,"下一次提醒",52,7,152);
    next_reminder=small(next,ui_reminder_summary(),52,31,152);
    lv_obj_t *note=small(ui_root,"点击项目，设置本次提醒",12,300,216);
    lv_obj_set_style_text_align(note,LV_TEXT_ALIGN_CENTER,0);
}
