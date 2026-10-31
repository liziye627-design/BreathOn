#include "ui_internal.h"

static lv_obj_t *voice_status_label;
static lv_obj_t *voice_transcript_label;
static lv_obj_t *network_label;



static void align_transcript(lv_event_t *event)
{
    lv_obj_t *viewport = lv_event_get_target(event);
    const lv_font_t *font = lv_obj_get_style_text_font(viewport, LV_PART_MAIN);
    int32_t pitch = font->line_height;
    int32_t y = lv_obj_get_scroll_y(viewport);
    int32_t end = y + lv_obj_get_scroll_bottom(viewport);
    int32_t target = ((y + pitch / 2) / pitch) * pitch;
    if (target > end) target = end;
    if (target < 0) target = 0;
    if (target != y) lv_obj_scroll_to_y(viewport, target, LV_ANIM_OFF);
}

static lv_obj_t *center_text(const char *text, int y)
{
    lv_obj_t *label = ui_text(ui_root, text, 12, y, 216);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    return label;
}

static lv_obj_t *mute_button, *mute_caption;
static lv_obj_t *device_voice;
static bool voice_available;
void ui_dashboard_network(const char *state);

void ui_pages_reset(void)
{
    ui_suggestions_reset();
    voice_status_label = voice_transcript_label = network_label = NULL;
    mute_button = mute_caption = device_voice = NULL;
}

void ui_home(const char *state) { ui_dashboard(state); }
void ui_voice_control_update(bool available)
{
    voice_available = available;
    ui_dashboard_voice(ui_current_voice(),available);
}

void ui_call_mute_update(bool available, bool muted)
{
    if (!mute_button) return;
    if (available) lv_obj_remove_state(mute_button,LV_STATE_DISABLED);
    else lv_obj_add_state(mute_button,LV_STATE_DISABLED);
    lv_label_set_text(mute_caption,muted ? "取消静音" : "静音");
    lv_obj_set_style_bg_color(mute_button,lv_color_hex(muted ? UI_ORANGE_LIGHT : 0x216B76),0);
    lv_obj_set_style_bg_grad_dir(mute_button,LV_GRAD_DIR_NONE,0);
}

void ui_voice(const char *state, const char *transcript)
{
    bool demo = breathon_ui_voice_demo_enabled();
    ui_nav_header(demo ? "通话演示" : "语音通话",BREATHON_HOME);
    lv_obj_t *halo=ui_panel(ui_root,66,51,108,108,UI_WASH);
    lv_obj_set_style_radius(halo,LV_RADIUS_CIRCLE,0);
    ui_motion_orb(44,false);
    ui_motion_voice_state(state);
    voice_status_label = center_text(state,162);
    lv_obj_set_height(voice_status_label,24);
    lv_label_set_long_mode(voice_status_label,LV_LABEL_LONG_DOT);
    lv_obj_t *transcript_box=ui_panel(ui_root,12,190,216,59,UI_WHITE);
    lv_obj_t *viewport=ui_panel(transcript_box,8,4,200,51,UI_WHITE);
    ui_small(viewport);
    lv_obj_set_style_text_line_space(viewport,0,0);
    lv_obj_set_style_radius(viewport,0,0);
    lv_obj_add_flag(viewport,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(viewport,LV_OBJ_FLAG_SCROLL_CHAIN_VER | LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_scroll_dir(viewport,LV_DIR_VER);
    lv_obj_set_scrollbar_mode(viewport,LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_event_cb(viewport,align_transcript,LV_EVENT_SCROLL_END,NULL);
    voice_transcript_label=ui_text(viewport,transcript,0,0,200);
    if (demo) {
        lv_obj_t *next=ui_action_button("换一句",12,264,104,false,breathon_ui_voice_demo_next,NULL);
        lv_obj_t *end=ui_button("结束演示",124,264,104,true,BREATHON_HOME);
        ui_small(lv_obj_get_child(next,0));
        ui_small(lv_obj_get_child(end,0));
        return;
    }
    mute_button=ui_action_button("",36,254,48,false,ui_mute_toggle,NULL);
    lv_obj_set_style_border_width(mute_button,0,0);
    ui_icon(mute_button,"mic",8,5,UI_WHITE);
    mute_caption=ui_text(ui_root,"静音",12,300,96); ui_small(mute_caption);
    lv_obj_set_style_text_align(mute_caption,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_t *hangup=ui_button("",156,254,48,false,BREATHON_HOME);
    lv_obj_set_style_bg_color(hangup,lv_color_hex(UI_RED),0);
    lv_obj_set_style_bg_grad_dir(hangup,LV_GRAD_DIR_NONE,0);
    lv_obj_set_style_border_width(hangup,0,0);
    ui_icon(hangup,"hangup",8,5,UI_WHITE);
    lv_obj_t *caption=ui_text(ui_root,"挂断",132,300,96); ui_small(caption);
    lv_obj_set_style_text_align(caption,LV_TEXT_ALIGN_CENTER,0);
    ui_call_mute_update(ui_mute_available(),ui_is_muted());
}
void ui_prepare(void)
{
    ui_header("开始检测");
    ui_orb(87, 66, 66);
    center_text("设备尚未准备好", 160);
    lv_obj_t *hint = center_text("目前无法进行检测\n暂无脂肪消耗估算", 200);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_SUBTLE), 0);
    ui_button("返回首页", 12, 264, 216, true, BREATHON_HOME);
}

void ui_result(void)
{
    ui_header("体验结束");
    ui_orb(87, 66, 66);
    center_text("本次没有检测结果", 160);
    lv_obj_t *hint = center_text("体验不会生成真实记录", 208);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_SUBTLE), 0);
    ui_button("返回首页", 12, 264, 216, true, BREATHON_HOME);
}

void ui_history(void)
{
    ui_header("消耗记录");
    lv_obj_t *paper = ui_panel(ui_root, 90, 68, 60, 64, UI_PALE);
    for (int i = 0; i < 3; ++i) {
        ui_panel(paper, 12, 16 + 14 * i, 36 - 6 * i, 3, UI_INK);
    }
    center_text("还没有记录", 156);
    lv_obj_t *hint = center_text("暂无可用的\n脂肪消耗估算", 190);
    lv_obj_set_style_text_color(hint, lv_color_hex(UI_SUBTLE), 0);
    ui_button("返回首页", 12, 264, 216, false, BREATHON_HOME);
}

void ui_device(const char *network)
{
    ui_nav_header("设备状态",BREATHON_HOME);
    lv_obj_t *panel=ui_panel(ui_root,12,53,216,82,UI_WHITE);
    lv_obj_t *wifi=ui_text(panel,"Wi-Fi",10,10,74); ui_small(wifi);
    network_label=ui_text(panel,network,88,10,118); ui_small(network_label);
    lv_obj_set_style_text_align(network_label,LV_TEXT_ALIGN_RIGHT,0);
    lv_obj_t *service=ui_text(panel,"语音服务",10,47,74); ui_small(service);
    device_voice=ui_text(panel,ui_current_voice(),88,47,118); ui_small(device_voice);
    lv_obj_set_height(device_voice,17); lv_label_set_long_mode(device_voice,LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(device_voice,LV_TEXT_ALIGN_RIGHT,0);
    ui_button("开始检测",12,148,104,false,BREATHON_PREPARE);
    ui_button("消耗记录",124,148,104,false,BREATHON_HISTORY);
    ui_button("体验流程",12,204,104,false,BREATHON_COLLECT);
    lv_obj_t *demo=ui_action_button(breathon_ui_demo_enabled()?"演示：开":"演示：关",124,204,104,false,ui_demo_toggle,NULL);
    ui_small(lv_obj_get_child(demo,0));
    lv_obj_t *note=center_text("灵息 · 固件 1.0.21",268); ui_small(note);
    lv_obj_t *hint=center_text("USB供电 · 传感器待接入",296); ui_small(hint);
}

void ui_voice_update(const char *state, const char *transcript)
{
    if (breathon_ui_page()==BREATHON_HOME) ui_dashboard_voice(state,voice_available);
    if (breathon_ui_page()==BREATHON_VOICE) ui_motion_voice_state(state);
    if (voice_status_label) lv_label_set_text(voice_status_label,state);
    if (voice_transcript_label) lv_label_set_text(voice_transcript_label,transcript);
    if (device_voice) lv_label_set_text(device_voice,state);
}

void ui_network_update(const char *network)
{
    if (breathon_ui_page()==BREATHON_HOME) ui_dashboard_network(network);
    if (network_label) lv_label_set_text(network_label,network);
}
