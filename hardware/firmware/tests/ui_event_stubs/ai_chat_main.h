#pragma once
#include "tal_api.h"
/* Narrow host seam for the pinned SDK's display event dispatcher. */
typedef enum {
    AI_MODE_STATE_INIT, AI_MODE_STATE_IDLE, AI_MODE_STATE_LISTEN,
    AI_MODE_STATE_UPLOAD, AI_MODE_STATE_THINK, AI_MODE_STATE_SPEAK,
    AI_MODE_STATE_INVALID
} AI_MODE_STATE_E;
typedef enum {
    AI_CHAT_MODE_HOLD, AI_CHAT_MODE_ONE_SHOT, AI_CHAT_MODE_WAKEUP, AI_CHAT_MODE_FREE
} AI_CHAT_MODE_E;
enum {
    AI_USER_EVT_ASR_OK, AI_USER_EVT_TEXT_STREAM_START, AI_USER_EVT_TEXT_STREAM_DATA,
    AI_USER_EVT_TEXT_STREAM_STOP, AI_USER_EVT_CHAT_BREAK, AI_USER_EVT_LLM_EMOTION,
    AI_USER_EVT_EMOTION, AI_USER_EVT_MODE_STATE_UPDATE, AI_USER_EVT_MODE_SWITCH
};
typedef struct { int type; void *data; } AI_NOTIFY_EVENT_T;
typedef struct { char *data; uint32_t datalen; } AI_NOTIFY_TEXT_T;
typedef struct { char *emoji; char *name; } AI_NOTIFY_EMO_T;
char *ai_get_mode_name_str(AI_CHAT_MODE_E mode);
