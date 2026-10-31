#include "ui_internal.h"
#include <stdio.h>

static lv_timer_t *reminder_timer;
static uint32_t reminder_started;
static enum { NONE, SCHEDULED, DUE, DONE } reminder_state;
static char summary[48];
static lv_obj_t *countdown;
static int selected_kind=1, active_kind=1;
static bool selection_pending;
static const char *actions[]={"补水","活动","休息"};
static const char *titles[]={"补水提醒","活动提醒","休息提醒"};
static const char *tips[]={"适量饮水，照顾自己","起身走走，放松一下","放松片刻，缓一缓"};
static const char *due[]={"该喝水啦","该活动一下啦","该休息一下啦"};
static const char *icons[]={"water","walk","moon"};
void ui_dashboard_reminder(void);
void ui_reminder_choose(int kind)
{
    if(kind<0 || kind>2) return;
    selected_kind=kind; selection_pending=true;
    breathon_ui_show(BREATHON_REMINDER);
}
void ui_reminder_current(lv_event_t *event)
{
    (void)event; selected_kind=active_kind; selection_pending=false;
    breathon_ui_show(BREATHON_REMINDER);
}

const char *ui_reminder_summary(void)
{
    if (reminder_state == DUE) return due[active_kind];
    if (reminder_state == DONE) return "本次已完成";
    if (reminder_state != SCHEDULED) return "还未设置提醒";
    uint32_t elapsed = lv_tick_elaps(reminder_started);
    unsigned minutes = elapsed < 600000 ? (600000-elapsed+59999)/60000 : 0;
    snprintf(summary,sizeof(summary),"%u分后%s",minutes,actions[active_kind]);
    return summary;
}

static void tick(lv_timer_t *timer)
{
    (void)timer;
    if (lv_tick_elaps(reminder_started) >= 600000) {
        reminder_state = DUE;
        lv_timer_delete(reminder_timer); reminder_timer = NULL;
        /* A reminder never steals an active call or replaces another task. */
        if (breathon_ui_page() == BREATHON_HOME) ui_reminder_current(NULL);
        else if (breathon_ui_page() == BREATHON_REMINDER && !selection_pending) ui_reminder_refresh();
        else if (breathon_ui_page() == BREATHON_SUGGESTIONS) ui_suggestions_reminder();
    } else if (breathon_ui_page() == BREATHON_HOME) ui_dashboard_reminder();
    else if (breathon_ui_page() == BREATHON_SUGGESTIONS) ui_suggestions_reminder();
    else if (breathon_ui_page() == BREATHON_REMINDER && !selection_pending && countdown)
        lv_label_set_text(countdown,ui_reminder_summary());
}

static void schedule(lv_event_t *event)
{
    (void)event;
    if (reminder_timer) lv_timer_delete(reminder_timer);
    reminder_started = lv_tick_get(); reminder_state = SCHEDULED;
    active_kind=selected_kind; selection_pending=false;
    reminder_timer = lv_timer_create(tick,1000,NULL);
    breathon_ui_show(BREATHON_HOME);
}

static void finish(lv_event_t *event)
{
    (void)event;
    if (reminder_timer) { lv_timer_delete(reminder_timer); reminder_timer = NULL; }
    reminder_state = reminder_state == SCHEDULED ? NONE : DONE;
    selection_pending=false;
    breathon_ui_show(BREATHON_HOME);
}

static lv_obj_t *center(const char *text, int y, bool small)
{
    lv_obj_t *label = ui_text(ui_root,text,12,y,216);
    if (small) ui_small(label);
    lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
    return label;
}

void ui_reminder(void)
{
    ui_nav_header("来自小息的提醒",BREATHON_HOME);
    lv_obj_t *halo = ui_panel(ui_root,70,64,100,100,UI_WASH);
    lv_obj_set_style_radius(halo,LV_RADIUS_CIRCLE,0);
    lv_obj_t *circle = ui_panel(halo,17,17,66,66,0x00BDB1);
    lv_obj_set_style_radius(circle,LV_RADIUS_CIRCLE,0);
    int kind=selection_pending?selected_kind:active_kind;
    ui_icon(circle,icons[kind],17,17,UI_WHITE);
    const char *title = !selection_pending && reminder_state == DUE ? due[kind] :
                        !selection_pending && reminder_state == SCHEDULED ? "提醒已设置" : titles[kind];
    center(title,179,false);
    countdown=center(!selection_pending && reminder_state == SCHEDULED ? ui_reminder_summary() : tips[kind],211,true);
    center("本次开机有效，重启后清除",239,true);
    lv_obj_t *later = ui_action_button(!selection_pending && reminder_state == SCHEDULED ? "重新计时" : "10分钟后",12,270,104,true,schedule,NULL);
    ui_small(lv_obj_get_child(later,0));
    lv_obj_t *done = !selection_pending && (reminder_state == SCHEDULED || reminder_state == DUE) ?
        ui_action_button(reminder_state == SCHEDULED ? "取消提醒" : "已完成",124,270,104,false,finish,NULL) :
        ui_button("返回首页",124,270,104,false,BREATHON_HOME);
    ui_small(lv_obj_get_child(done,0));
}

void ui_reminder_refresh(void) { breathon_ui_show(BREATHON_REMINDER); }

