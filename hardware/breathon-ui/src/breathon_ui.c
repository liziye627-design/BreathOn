#include "ui_internal.h"
#include <stdio.h>

lv_obj_t *ui_root;
static breathon_page_t current_page;
static bool network_known;
static bool network_connected;
static char voice_state[64] = "等待语音状态";
static char voice_text[1024] = "";
static breathon_voice_handler_t voice_handler;
static breathon_voice_handler_t active_voice_handler;
static breathon_mute_handler_t mute_handler;
static bool muted;

bool ui_mute_available(void) { return mute_handler != NULL && active_voice_handler != NULL; }
bool ui_is_muted(void) { return muted; }
const char *ui_current_voice(void) { return voice_state; }
void breathon_ui_set_mute_handler(breathon_mute_handler_t handler)
{
    if (muted && mute_handler && !mute_handler(false)) return;
    muted = false;
    mute_handler = handler;
    if (ui_root && current_page == BREATHON_VOICE) ui_call_mute_update(ui_mute_available(), muted);
}
void ui_mute_toggle(lv_event_t *event)
{
    (void)event;
    if (ui_mute_available() && mute_handler(!muted)) muted = !muted;
    ui_call_mute_update(ui_mute_available(), muted);
}

static void stop_voice(void)
{
    breathon_voice_handler_t handler = active_voice_handler;
    active_voice_handler = NULL;
    if (handler) handler(false);
    muted = false;
    if (current_page == BREATHON_VOICE) ui_call_mute_update(false, false);
}

void breathon_ui_set_voice_handler(breathon_voice_handler_t handler)
{
    if (handler != voice_handler) stop_voice();
    voice_handler = handler;
    if (ui_root && current_page == BREATHON_HOME) {
        ui_voice_control_update(handler != NULL);
    }
}

void ui_voice_start(lv_event_t *event)
{
    (void)event;
    if (current_page != BREATHON_HOME || !voice_handler) return;
    breathon_ui_show(BREATHON_VOICE);
    active_voice_handler = voice_handler;
    active_voice_handler(true);
    muted = false;
    ui_call_mute_update(ui_mute_available(), muted);
}

const char *ui_network_text(void)
{
    return !network_known ? "待确认" : (network_connected ? "已连接" : "未连接");
}

void breathon_ui_init(const lv_font_t *chinese_font)
{
    ui_root = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_root, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_grad_color(ui_root, lv_color_hex(UI_BG_END), 0);
    lv_obj_set_style_bg_grad_dir(ui_root, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_text_font(ui_root, chinese_font, 0);
    lv_obj_set_style_text_color(ui_root, lv_color_hex(UI_TEXT), 0);
    lv_obj_set_style_pad_all(ui_root, 0, 0);
    lv_obj_remove_flag(ui_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_screen_load(ui_root);
    breathon_ui_show(BREATHON_HOME);
}

void breathon_ui_show(breathon_page_t page)
{
    if (!ui_root || page < BREATHON_HOME || page > BREATHON_SUGGESTIONS) {
        return;
    }
    if (current_page == BREATHON_AUDIO && page != BREATHON_AUDIO) ui_audio_leave();
    if (current_page == BREATHON_VOICE && page != BREATHON_VOICE) stop_voice();
    ui_motion_cancel();
    ui_collect_cancel();
    current_page = page;
    ui_pages_reset();
    lv_obj_clean(ui_root);
    switch (page) {
    case BREATHON_HOME:
        ui_home(voice_state);
        ui_voice_control_update(voice_handler != NULL);
        break;
    case BREATHON_VOICE: ui_voice(voice_state, voice_text); break;
    case BREATHON_PREPARE: ui_prepare(); break;
    case BREATHON_COLLECT: ui_collect(); break;
    case BREATHON_RESULT: ui_result(); break;
    case BREATHON_HISTORY: ui_history(); break;
    case BREATHON_DEVICE: ui_device(ui_network_text()); break;
    case BREATHON_AUDIO: ui_audio(); break;
    case BREATHON_ACTIVITY: ui_activity(); break;
    case BREATHON_REMINDER: ui_reminder(); break;
    case BREATHON_GAS: ui_gas(); break;
    case BREATHON_RELATION: ui_relation(); break;
    case BREATHON_INSIGHT: ui_insight(); break;
    case BREATHON_SUGGESTIONS: ui_suggestions(); break;
    }
}

void breathon_ui_set_network(bool known, bool connected)
{
    network_known = known;
    network_connected = connected;
    if (ui_root && (current_page == BREATHON_DEVICE || current_page == BREATHON_HOME)) {
        ui_network_update(ui_network_text());
    }
}

/* Copy complete UTF-8 codepoints so long ASR text cannot leave a broken glyph. */
void ui_copy_text(char *output, size_t size, const char *input)
{
    snprintf(output, size, "%s", input);
    size_t n = 0;
    while (output[n]) {
        unsigned char c = (unsigned char)output[n];
        size_t width = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        for (size_t i = 1; i < width; ++i) {
            if (output[n + i] == '\0') {
                output[n] = '\0';
                return;
            }
        }
        n += width;
    }
}

void breathon_ui_set_voice(const char *state, const char *transcript)
{
    if (state) ui_copy_text(voice_state, sizeof(voice_state), state);
    if (transcript) ui_copy_text(voice_text, sizeof(voice_text), transcript);
    if (ui_root && (current_page == BREATHON_HOME || current_page == BREATHON_VOICE || current_page == BREATHON_DEVICE)) {
        ui_voice_update(voice_state, voice_text);
    }
}

breathon_page_t breathon_ui_page(void)
{
    return current_page;
}
