#ifndef TEST_RT_UI_H
#define TEST_RT_UI_H
#include "tal_api.h"
typedef enum { BREATHON_AUDIO_RECORD, BREATHON_AUDIO_PLAY, BREATHON_AUDIO_STOP, BREATHON_AUDIO_TONE } breathon_audio_action_t;
typedef struct { const char *state; uint32_t elapsed_ms, level; bool has_recording, busy; } breathon_audio_status_t;
#endif
