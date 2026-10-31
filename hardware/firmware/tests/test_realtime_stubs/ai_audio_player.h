#ifndef TEST_RT_AI_PLAYER_H
#define TEST_RT_AI_PLAYER_H
#include "svc_ai_player.h"
typedef enum { AI_AUDIO_PLAYER_TTS_START, AI_AUDIO_PLAYER_TTS_DATA, AI_AUDIO_PLAYER_TTS_STOP, AI_AUDIO_PLAYER_TTS_ABORT } AI_AUDIO_PLAYER_TTS_STATE_E;
enum { AI_AUDIO_PLAYER_ALL, AI_AUDIO_ALERT_WAKEUP };
OPERATE_RET ai_audio_play_tts_stream(AI_AUDIO_PLAYER_TTS_STATE_E, AI_AUDIO_CODEC_E, char *, int);
OPERATE_RET ai_audio_player_stop(int);
bool ai_audio_player_is_playing(void);
OPERATE_RET ai_audio_player_alert(int);
#endif
