#include "ui_internal.h"
#include <string.h>
#include <stdio.h>

static lv_obj_t *entry, *hint, *network, *reminder_summary;
const char *ui_reminder_summary(void);

void ui_small(lv_obj_t *label)
{
    lv_obj_set_style_text_font(label, &font_puhui_14_1, 0);
}

/* Vector waves stay behind controls; clipped by the root display. */
void ui_background(void)
{
    for (int i = 0; i < 2; ++i) {
        lv_obj_t *wave = ui_panel(ui_root, -24 + i * 104, 292 + i * 6, 184, 64,
                                  i ? 0x086F76 : UI_WASH);
        lv_obj_set_style_radius(wave, LV_RADIUS_CIRCLE, 0);
        lv_obj_remove_flag(wave, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_t *corner = ui_panel(ui_root, 164, -55, 140, 170, 0x0A4550);
    lv_obj_set_style_radius(corner, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(corner, LV_OBJ_FLAG_CLICKABLE);
}

static void icon_line(lv_obj_t *parent, const lv_point_precise_t *p, int n, uint32_t color)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_obj_remove_style_all(line);
    lv_line_set_points(line, p, n);
    lv_obj_set_style_line_width(line, 3, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(color), 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
}

static void passive(lv_obj_t *object)
{
    lv_obj_remove_flag(object,LV_OBJ_FLAG_CLICKABLE);
    for(uint32_t i=0;i<lv_obj_get_child_count(object);++i)
        passive(lv_obj_get_child(object,(int32_t)i));
}

void ui_icon(lv_obj_t *parent, const char *kind, int x, int y, uint32_t color)
{
    lv_obj_t *box = ui_panel(parent, x, y, 32, 32, UI_BG);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
    static const lv_point_precise_t phone[] = {{7,5},{4,9},{8,18},{15,25},{23,28},{28,23},{22,18},{18,22},{10,14},{13,10},{7,5}};
    static const lv_point_precise_t bell[] = {{5,24},{8,20},{8,12},{10,7},{16,5},{22,7},{24,12},{24,20},{27,24},{5,24}};
    static const lv_point_precise_t fire[] = {{17,3},{13,11},{15,14},{20,11},{24,16},{25,22},{21,28},{12,28},{6,23},{7,17},{10,11}};
    static const lv_point_precise_t walk[] = {{21,12},{16,9},{12,18},{22,22},{25,28}};
    static const lv_point_precise_t leg[] = {{12,18},{8,28}};
    static const lv_point_precise_t arm[] = {{14,12},{21,16},{26,16}};
    static const lv_point_precise_t mic[] = {{6,17},{6,20},{9,25},{16,27},{23,25},{26,20},{26,17}};
    static const lv_point_precise_t hangup[] = {{4,22},{7,16},{15,12},{20,12},{27,17},{29,22}};
    if (!strcmp(kind,"phone")) icon_line(box,phone,sizeof(phone)/sizeof(phone[0]),color);
    else if (!strcmp(kind,"bell")) {
        icon_line(box,bell,sizeof(bell)/sizeof(bell[0]),color); ui_panel(box,13,27,6,3,color);
    } else if (!strcmp(kind,"fire")) icon_line(box,fire,11,color);
    else if (!strcmp(kind,"lungs")) {
        lv_obj_t *left=ui_panel(box,3,10,11,20,color);
        lv_obj_t *right=ui_panel(box,18,10,11,20,color);
        lv_obj_set_style_radius(left,8,0); lv_obj_set_style_radius(right,8,0);
        ui_panel(box,14,2,4,19,color);
    } else if (!strcmp(kind,"calendar")) {
        lv_obj_t *frame=ui_panel(box,4,6,24,23,color);
        lv_obj_set_style_bg_opa(frame,LV_OPA_TRANSP,0);
        lv_obj_set_style_border_width(frame,3,0);
        lv_obj_set_style_border_color(frame,lv_color_hex(color),0);
        ui_panel(box,8,2,3,9,color); ui_panel(box,21,2,3,9,color);
        ui_panel(box,6,12,20,3,color);
    } else if (!strcmp(kind,"water")) {
        static const lv_point_precise_t drop[]={{16,2},{7,15},{5,23},{9,29},{16,31},{23,29},{27,23},{25,15},{16,2}};
        icon_line(box,drop,9,color);
    } else if (!strcmp(kind,"moon")) {
        /* An open-background crescent also works on the teal reminder badge. */
        static const lv_point_precise_t moon[]={{22,3},{14,3},{7,8},{4,15},{6,23},{12,28},{20,29},{27,25},{21,24},{16,20},{13,14},{14,8},{18,4},{22,3}};
        icon_line(box,moon,sizeof(moon)/sizeof(moon[0]),color);
    }
    else if (!strcmp(kind,"walk")) {
        ui_panel(box,16,0,6,6,color); icon_line(box,walk,5,color);
        icon_line(box,leg,2,color); icon_line(box,arm,3,color);
    } else if (!strcmp(kind,"mic")) {
        ui_panel(box,12,3,8,18,color); icon_line(box,mic,7,color);
        ui_panel(box,15,27,3,5,color);
    } else icon_line(box,hangup,6,color);
    passive(box);
}

void ui_nav_header(const char *title, breathon_page_t back)
{
    ui_background();
    lv_obj_t *label = ui_text(ui_root, title, 44, 14, 184);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *button = ui_button("‹", 0, 0, 44, false, back);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_text_color(button, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
}

lv_obj_t *ui_nav_card(int y, int h, breathon_page_t target)
{
    lv_obj_t *card = target == BREATHON_VOICE ?
        ui_action_button("",12,y,216,false,ui_voice_start,NULL) :
        ui_button("", 12, y, 216, false, target);
    lv_obj_set_height(card, h);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(UI_CARD_END), 0);
    /* Keep muted card copy readable while pressing; outline carries feedback. */
    lv_obj_set_style_bg_color(card,lv_color_hex(UI_WHITE),LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_color(card,lv_color_hex(UI_WHITE),LV_STATE_PRESSED);
    lv_obj_set_style_outline_color(card,lv_color_hex(UI_CYAN),LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(card,2,LV_STATE_PRESSED);
    return card;
}

static lv_obj_t *home_card(int x, int y, breathon_page_t target, const char *kind, uint32_t color)
{
    lv_obj_t *card=ui_nav_card(y,104,target);
    lv_obj_set_pos(card,x,y); lv_obj_set_width(card,104);
    lv_obj_t *circle=ui_panel(card,10,9,32,32,UI_ICON_WASH);
    lv_obj_set_style_radius(circle,LV_RADIUS_CIRCLE,0);
    lv_obj_remove_flag(circle,LV_OBJ_FLAG_CLICKABLE);
    ui_icon(circle,kind,0,0,color);
    return card;
}
static lv_obj_t *card_copy(lv_obj_t *card,const char *text,int y)
{
    lv_obj_t *label=ui_text(card,text,10,y,84); ui_small(label);
    lv_obj_set_height(label,17); lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);
    return label;
}
void ui_dashboard(const char *state)
{
    ui_background();
    char connection[48]; snprintf(connection,sizeof(connection),"Wi-Fi · %s",ui_network_text());
    network=ui_text(ui_root,connection,12,10,156); ui_small(network);
    lv_obj_set_style_text_color(network,lv_color_hex(UI_SUBTLE),0);
    lv_obj_t *tools=ui_button("设置",184,0,56,false,BREATHON_DEVICE);
    ui_small(lv_obj_get_child(tools,0));
    lv_obj_set_style_bg_opa(tools,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(tools,0,0);
    lv_obj_set_style_text_color(tools,lv_color_hex(UI_TEXT),0);
    lv_obj_set_style_bg_opa(tools,LV_OPA_COVER,LV_STATE_PRESSED);
    ui_text(ui_root,"灵息 · Breath On",12,35,216);
    lv_obj_t *intro=ui_text(ui_root,"一口呼吸，轻松一点",12,59,216); ui_small(intro);
    lv_obj_t *gas=home_card(12,80,BREATHON_GAS,"lungs",UI_TEAL);
    ui_metric_badge(gas,58,14);
    card_copy(gas,"气体输入",47);
    ui_metric_number(gas,breathon_ui_demo_enabled()?"1.26":"--",10,64,84);
    lv_obj_t *activity=home_card(124,80,BREATHON_ACTIVITY,"fire",UI_ORANGE);
    card_copy(activity,"今日活动消耗",47);
    ui_metric_number(activity,breathon_ui_demo_enabled()?"326":"--",10,64,64);
    lv_obj_t *unit=ui_text(activity,"kcal",72,81,28);
    lv_obj_set_style_text_font(unit,&lv_font_montserrat_12,0);
    entry=home_card(12,192,BREATHON_VOICE,"phone",UI_TEAL);
    card_copy(entry,"和小息通话",47);
    hint=card_copy(entry,state,74);
    lv_obj_set_style_text_color(hint,lv_color_hex(UI_MUTED),0);
    lv_obj_t *reminder=home_card(124,192,BREATHON_SUGGESTIONS,"calendar",UI_ORANGE);
    card_copy(reminder,"下一个提醒",47);
    reminder_summary=card_copy(reminder,ui_reminder_summary(),74);
    lv_obj_set_style_text_color(reminder_summary,lv_color_hex(UI_MUTED),0);
    lv_obj_t *footer=ui_text(ui_root,ui_demo_note(),12,299,216); ui_small(footer);
    lv_obj_set_style_text_align(footer,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_color(footer,lv_color_hex(UI_SUBTLE),0);
}
void ui_dashboard_voice(const char *state,bool available)
{
    if(available) lv_obj_remove_state(entry,LV_STATE_DISABLED);
    else lv_obj_add_state(entry,LV_STATE_DISABLED);
    lv_label_set_text(hint,available?(!strcmp(state,"待命")?"语音陪伴":state):"语音暂未接入");
}
void ui_dashboard_network(const char *state) { lv_label_set_text_fmt(network,"Wi-Fi · %s",state); }
void ui_dashboard_reminder(void) { lv_label_set_text(reminder_summary,ui_reminder_summary()); }

