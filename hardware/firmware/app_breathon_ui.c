/* TuyaOpen 695e571f: connect the board UI to the existing AI event dispatcher. */
#include "tal_api.h"
#include "lv_vendor.h"
#include "ai_ui_manage.h"
#include "ai_ui_icon_font.h"
#include "breathon_ui.h"
#include "app_breathon_audio.h"
#include "app_breathon_realtime.h"

static char stream_text[1024];
static bool stream_active;
static bool ui_ready;

static void voice_action(bool start)
{
    if (start) app_breathon_realtime_start();
    else app_breathon_realtime_stop();
}

void app_breathon_realtime_publish(const char *state, const char *text)
{
    if (BREATHON_PRESENTATION_ONLY || !ui_ready || state == NULL) return;
    const char *label = NULL;
    const char *message = text;
    if (strcmp(state, "connecting") == 0) {
        label = "正在连接";
        message = "正在连接语音服务";
    } else if (strcmp(state, "listening") == 0) label = "聆听中";
    else if (strcmp(state, "thinking") == 0) label = "思考中";
    else if (strcmp(state, "speaking") == 0) label = "播放中";
    else if (strcmp(state, "stopped") == 0 || strcmp(state, "configured") == 0) {
        label = "待命";
        message = "";
    } else if (strcmp(state, "error") == 0) {
        label = text && strstr(text,"网关") ? "网关未连接" : "语音暂不可用";
        message = (text && text[0]) ? text : "请稍后重试";
    } else if (strcmp(state, "transcript_user") != 0 &&
               strcmp(state, "transcript_assistant") != 0) return;
    lv_vendor_disp_lock();
    stream_active = false;
    breathon_ui_set_voice(label, message);
    lv_vendor_disp_unlock();
}

void app_breathon_audio_publish(const breathon_audio_status_t *status)
{
    if (!ui_ready) return;
    lv_vendor_disp_lock();
    breathon_ui_set_audio_handler(app_breathon_audio_action);
    breathon_ui_set_audio(status);
    lv_vendor_disp_unlock();
}

static OPERATE_RET display_init(void)
{
    lv_vendor_init(DISPLAY_NAME);
    lv_vendor_start(5, 1024 * 8);
    lv_vendor_disp_lock();
    lv_display_t *display = lv_display_get_default();
    if (display == NULL) {
        lv_vendor_disp_unlock();
        return OPRT_COM_ERROR;
    }
    /* Native panel/touch coordinates are 240x320; this product uses portrait. */
    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_0);
    if (lv_display_get_horizontal_resolution(display) != 240 ||
        lv_display_get_vertical_resolution(display) != 320) {
        lv_vendor_disp_unlock();
        PR_ERR("BREATHON_UI: unexpected panel resolution");
        return OPRT_INVALID_PARM;
    }
    breathon_ui_init(ai_ui_get_text_font());
    if (BREATHON_PRESENTATION_ONLY) {
        breathon_ui_enable_voice_demo();
    } else {
        breathon_ui_set_voice_handler(voice_action);
        breathon_ui_set_mute_handler(app_breathon_realtime_mute);
        breathon_ui_set_voice(INITIALIZING, "正在准备语音服务");
    }
    ui_ready = true;
    lv_vendor_disp_unlock();
    PR_NOTICE("BREATHON_UI_V1: portrait 240x320 ready; firmware %s", PROJECT_VERSION);
    return OPRT_OK;
}

static void show_status(char *status)
{
    if (BREATHON_PRESENTATION_ONLY) return;
    if (status == NULL || !ui_ready || app_breathon_realtime_configured() || app_breathon_realtime_active()) return;
    lv_vendor_disp_lock();
    if (ui_ready) breathon_ui_set_voice(status, NULL);
    lv_vendor_disp_unlock();
}

static void show_text(char *text)
{
    if (BREATHON_PRESENTATION_ONLY) return;
    if (text == NULL || !ui_ready || app_breathon_realtime_configured() || app_breathon_realtime_active()) return;
    lv_vendor_disp_lock();
    if (ui_ready) breathon_ui_set_voice(NULL, text);
    lv_vendor_disp_unlock();
}

static void show_network(AI_UI_WIFI_STATUS_E status)
{
    if (!ui_ready) return;
    lv_vendor_disp_lock();
    if (ui_ready) breathon_ui_set_network(true, status != AI_UI_WIFI_STATUS_DISCONNECTED);
    lv_vendor_disp_unlock();
}

static void show_mode(char *mode)
{
    if (mode == NULL) return;
    char hint[160];
    snprintf(hint, sizeof(hint), "当前模式：%s\n说出唤醒词开始对话", mode);
    show_text(hint);
}

static void stream_start(void)
{
    if (BREATHON_PRESENTATION_ONLY) return;
    if (!ui_ready || app_breathon_realtime_configured() || app_breathon_realtime_active()) return;
    lv_vendor_disp_lock();
    stream_text[0] = '\0';
    stream_active = true;
    if (ui_ready) breathon_ui_set_voice(NULL, "");
    lv_vendor_disp_unlock();
}

static void stream_append(char *text)
{
    if (BREATHON_PRESENTATION_ONLY) return;
    if (text == NULL || !ui_ready || app_breathon_realtime_configured() || app_breathon_realtime_active()) return;
    lv_vendor_disp_lock();
    if (stream_active && ui_ready) {
        size_t used = strlen(stream_text);
        snprintf(stream_text + used, sizeof(stream_text) - used, "%s", text);
        breathon_ui_set_voice(NULL, stream_text);
    }
    lv_vendor_disp_unlock();
}

static void stream_end(void)
{
    if (BREATHON_PRESENTATION_ONLY) return;
    if (!ui_ready || app_breathon_realtime_configured() || app_breathon_realtime_active()) return;
    lv_vendor_disp_lock();
    stream_active = false;
    lv_vendor_disp_unlock();
}

OPERATE_RET app_breathon_ui_register(void)
{
    AI_UI_INTFS_T display = {0};
    display.disp_init = display_init;
    display.disp_ai_mode_state = show_status;
    display.disp_notification = show_text;
    display.disp_wifi_state = show_network;
    display.disp_ai_chat_mode = show_mode;
    OPERATE_RET rt = ai_ui_register(&display);
    if (rt != OPRT_OK) return rt;
    AI_UI_CHAT_INTFS_T chat = {0};
    chat.disp_user_msg = show_text;
    chat.disp_ai_msg = show_text;
    chat.disp_system_msg = show_text;
    chat.disp_ai_msg_stream_start = stream_start;
    chat.disp_ai_msg_stream_data = stream_append;
    chat.disp_ai_msg_stream_end = stream_end;
    return ai_ui_chat_register(&chat);
}
