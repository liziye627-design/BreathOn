#ifndef APP_BREATHON_AUDIO_H
#define APP_BREATHON_AUDIO_H
#include "tal_api.h"
#include "ai_user_event.h"
#include "audio_capture.h"
#include "breathon_ui.h"
enum { BREATHON_AUDIO_EXPORT = 4, BREATHON_AUDIO_STATUS = 5 };
OPERATE_RET app_breathon_audio_init(void);
void app_breathon_audio_event(AI_NOTIFY_EVENT_T *event);
void app_breathon_audio_request(int action);
void app_breathon_audio_action(breathon_audio_action_t action);
bool ai_audio_local_capture_active(void);
typedef enum { BREATHON_OWNER_NONE, BREATHON_OWNER_LOCAL, BREATHON_OWNER_REALTIME } breathon_audio_owner_t;
/* Worker context only. Ownership stays claimed until player stop has completed. */
bool app_breathon_audio_acquire(breathon_audio_owner_t owner);
void app_breathon_audio_release(breathon_audio_owner_t owner);
breathon_audio_owner_t app_breathon_audio_owner(void);
void app_breathon_audio_publish(const breathon_audio_status_t *status);
OPERATE_RET app_breathon_audio_cli_register(void);
void app_breathon_audio_export(const audio_capture_t *capture);
bool app_breathon_audio_stop_pending(void);
#endif
