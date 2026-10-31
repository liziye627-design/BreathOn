#ifndef APP_BREATHON_REALTIME_H
#define APP_BREATHON_REALTIME_H
#include "tal_api.h"
#include "ai_user_event.h"

#include "app_breathon_realtime_mode.h"

/* Init once after audio init; registers CLI only, never opens a session.
 * Forward MIC_DATA from the existing user-event hook (16k/s16le/mono).
 * start/stop are asynchronous and safe for UI handlers. active includes pending,
 * connecting and cleanup. A new explicit start is required after every failure.
 * Explicit config persists its LAN endpoint/token in an independent KV record.
 * Init loads that record, never starts; token is never printed. */
OPERATE_RET app_breathon_realtime_init(void);
void app_breathon_realtime_event(AI_NOTIFY_EVENT_T *event);
void app_breathon_realtime_start(void);
void app_breathon_realtime_stop(void);
/* Gate future microphone samples; playback remains active. */
bool app_breathon_realtime_mute(bool muted);
bool app_breathon_realtime_active(void);
bool app_breathon_realtime_configured(void);
OPERATE_RET app_breathon_realtime_config(const char *ipv4, uint16_t port, const char *token);
void app_breathon_realtime_status(void);
/* Parent UI bridge MUST copy text before returning; called on network worker.
 * state: connecting/listening/thinking/speaking/stopped/error/configured,
 * transcript_user/transcript_assistant. text is bounded to 512 UTF-8 bytes. */
void app_breathon_realtime_publish(const char *state, const char *text);
#endif
