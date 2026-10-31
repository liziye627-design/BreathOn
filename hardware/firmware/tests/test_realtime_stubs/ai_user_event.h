#ifndef TEST_RT_USER_EVENT_H
#define TEST_RT_USER_EVENT_H
#include "tal_api.h"
enum { AI_USER_EVT_MIC_DATA, AI_USER_EVT_EXIT };
typedef struct { int type; void *data; } AI_NOTIFY_EVENT_T;
typedef struct { uint8_t *data; uint32_t data_len; } AI_NOTIFY_MIC_DATA_T;
void ai_user_event_notify(int type, void *data);
#endif
