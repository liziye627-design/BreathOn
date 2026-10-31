#pragma once
#include "tal_api.h"
#define EMOJI_NEUTRAL "neutral"
enum {
    AI_UI_DISP_EMOTION, AI_UI_DISP_STATUS, AI_UI_DISP_USER_MSG,
    AI_UI_DISP_AI_MSG_STREAM_START, AI_UI_DISP_AI_MSG_STREAM_DATA,
    AI_UI_DISP_AI_MSG_STREAM_END, AI_UI_DISP_AI_MSG_STREAM_INTERRUPT,
    AI_UI_DISP_CHAT_MODE
};
void ai_ui_disp_msg(int type, uint8_t *data, uint32_t len);
int ai_ui_init(void);
