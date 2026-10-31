#include <stdio.h>
#include "ui_event_stubs/tal_api.h"
#include "ui_event_stubs/ai_ui_manage.h"
#include "ui_event_stubs/ai_chat_main.h"
#include "lang_config.h"

static char observed[64];
static unsigned status_events;
void ai_ui_disp_msg(int type, uint8_t *data, uint32_t len)
{
    if (type != AI_UI_DISP_STATUS) return;
    size_t count = len < sizeof(observed) - 1 ? len : sizeof(observed) - 1;
    memcpy(observed, data, count);
    observed[count] = '\0';
    ++status_events;
}
int ai_ui_init(void) { return OPRT_OK; }
char *ai_get_mode_name_str(AI_CHAT_MODE_E mode) { (void)mode; return "test"; }
void ai_chat_ui_handle_event(AI_NOTIFY_EVENT_T *event);
void ai_chat_ui_refresh_locale(void);

static bool expect_state(AI_MODE_STATE_E state, const char *expected)
{
    unsigned before = status_events;
    AI_NOTIFY_EVENT_T event = {AI_USER_EVT_MODE_STATE_UPDATE, (void *)(uintptr_t)state};
    ai_chat_ui_handle_event(&event);
    if (status_events != before + 1 || strcmp(observed, expected)) {
        fprintf(stderr, "FAIL state=%d: missing or incorrect display status\n", state);
        return false;
    }
    return true;
}

int main(void)
{
    ai_chat_ui_handle_event(NULL);
    if (!expect_state(AI_MODE_STATE_LISTEN, LISTENING)) return 1;
    if (!expect_state(AI_MODE_STATE_THINK, THINKING)) return 1;
    unsigned before = status_events;
    ai_chat_ui_refresh_locale();
    if (status_events != before + 1 || strcmp(observed, THINKING)) return 2;
    if (!expect_state(AI_MODE_STATE_SPEAK, SPEAKING)) return 1;
    if (!expect_state(AI_MODE_STATE_IDLE, STANDBY)) return 1;
    puts("PASS: real SDK dispatcher LISTEN -> THINK -> refresh -> SPEAK -> IDLE");
    return 0;
}
