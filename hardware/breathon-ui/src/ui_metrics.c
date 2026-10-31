#include "ui_internal.h"

static bool demo_enabled=true;
bool breathon_ui_demo_enabled(void) { return demo_enabled; }
const char *ui_demo_note(void) { return demo_enabled?"模拟数据 · 仅供展示":"传感器待接入"; }
void breathon_ui_set_demo(bool enabled)
{
    if(demo_enabled==enabled) return;
    demo_enabled=enabled;
    if(!ui_root) return;
    breathon_page_t page=breathon_ui_page();
    if(page==BREATHON_HOME || page==BREATHON_GAS || page==BREATHON_ACTIVITY ||
       page==BREATHON_RELATION || page==BREATHON_INSIGHT || page==BREATHON_DEVICE)
        breathon_ui_show(page);
}
void ui_demo_toggle(lv_event_t *event) { (void)event; breathon_ui_set_demo(!demo_enabled); }
void ui_metric_number(lv_obj_t *parent,const char *value,int x,int y,int width)
{
    lv_obj_t *label=ui_text(parent,value,x,y,width);
    lv_obj_set_style_text_font(label,&lv_font_montserrat_32,0);
}
void ui_metric_badge(lv_obj_t *parent,int x,int y)
{
    lv_obj_t *pill=ui_panel(parent,x,y,38,21,UI_ROSE);
    lv_obj_remove_flag(pill,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *label=ui_text(pill,demo_enabled?"模拟":"待接",0,2,38); ui_small(label);
    lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
}
static lv_obj_t *small(lv_obj_t *parent,const char *text,int x,int y,int width)
{
    lv_obj_t *label=ui_text(parent,text,x,y,width); ui_small(label); return label;
}
static void note(void)
{
    lv_obj_t *label=small(ui_root,ui_demo_note(),12,299,216);
    lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
}
void ui_gas(void)
{
    ui_nav_header("气体输入",BREATHON_HOME);
    lv_obj_t *plot=ui_panel(ui_root,12,48,216,62,UI_WASH);
    if(demo_enabled) ui_chart_lines(plot,0,true);
    else { lv_obj_t *empty=small(plot,"等待传感器数据",0,22,216); lv_obj_set_style_text_color(empty,lv_color_hex(UI_TEXT),0); lv_obj_set_style_text_align(empty,LV_TEXT_ALIGN_CENTER,0); }
    lv_obj_t *card=ui_panel(ui_root,12,118,216,70,UI_WHITE);
    ui_icon(card,"lungs",12,23,UI_TEAL);
    small(card,"呼气特征值",56,8,130);
    ui_metric_number(card,demo_enabled?"1.26":"--",56,31,90);
    ui_metric_badge(card,166,36);
    const char *names[]={"趋势","样例状态"};
    const char *values[]={demo_enabled?"+0.12":"--",demo_enabled?"模拟中":"未接入"};
    for(int i=0;i<2;++i) {
        lv_obj_t *tile=ui_panel(ui_root,12+i*112,196,104,48,UI_WHITE);
        small(tile,names[i],10,4,84);
        lv_obj_t *value=ui_text(tile,values[i],10,24,84);
        lv_obj_set_style_text_color(value,lv_color_hex(UI_INK),0);
    }
    lv_obj_t *next=ui_button("查看关系分析  ›",12,252,216,false,BREATHON_RELATION);
    ui_small(lv_obj_get_child(next,0)); note();
}
void ui_activity(void)
{
    ui_nav_header("热量估算",BREATHON_HOME);
    lv_obj_t *hero=ui_panel(ui_root,12,48,216,92,UI_WHITE);
    lv_obj_t *circle=ui_panel(hero,10,15,48,48,UI_ROSE);
    lv_obj_set_style_radius(circle,LV_RADIUS_CIRCLE,0);
    ui_icon(circle,"fire",8,8,UI_ORANGE);
    small(hero,"活动热量",70,10,126);
    ui_metric_number(hero,demo_enabled?"326":"--",70,31,84);
    small(hero,"kcal",153,46,52);
    lv_obj_t *source=small(hero,demo_enabled?"活动与气体输入的模拟估算":"连接数据源后显示估算",10,72,196);
    lv_obj_set_style_text_align(source,LV_TEXT_ALIGN_CENTER,0);
    const char *names[]={"步数","心率","气体"};
    const char *values[]={demo_enabled?"3628":"--",demo_enabled?"92":"--",demo_enabled?"1.26":"--"};
    const char *units[]={"步","次/分",demo_enabled?"模拟":"待接入"};
    for(int i=0;i<3;++i) {
        lv_obj_t *tile=ui_panel(ui_root,12+i*74,148,68,91,UI_WHITE);
        small(tile,names[i],0,9,68);
        lv_obj_t *value=ui_text(tile,values[i],0,33,68);
        lv_obj_t *unit=small(tile,units[i],0,62,68);
        lv_obj_set_style_text_align(lv_obj_get_child(tile,0),LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_style_text_align(value,LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_style_text_align(unit,LV_TEXT_ALIGN_CENTER,0);
    }
    lv_obj_t *next=ui_button("气体 × 热量  ›",12,249,216,false,BREATHON_RELATION);
    ui_small(lv_obj_get_child(next,0)); note();
}
