#ifndef BREATHON_UI_H
#define BREATHON_UI_H
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

typedef enum {
    BREATHON_HOME, BREATHON_VOICE, BREATHON_PREPARE, BREATHON_COLLECT,
    BREATHON_RESULT, BREATHON_HISTORY, BREATHON_DEVICE, BREATHON_AUDIO,
    BREATHON_ACTIVITY, BREATHON_REMINDER, BREATHON_GAS, BREATHON_RELATION,
    BREATHON_INSIGHT, BREATHON_SUGGESTIONS
} breathon_page_t;

typedef void (*breathon_voice_handler_t)(bool start);
typedef bool (*breathon_mute_handler_t)(bool muted);
/* The sink applies the capture gate synchronously; false leaves UI unchanged. */
void breathon_ui_set_mute_handler(breathon_mute_handler_t handler);

typedef enum breathon_audio_action_t {
    BREATHON_AUDIO_RECORD, BREATHON_AUDIO_PLAY, BREATHON_AUDIO_STOP, BREATHON_AUDIO_TONE
} breathon_audio_action_t;
typedef void (*breathon_audio_handler_t)(breathon_audio_action_t);
typedef struct breathon_audio_status_t {
    const char *state;
    uint32_t elapsed_ms;
    uint8_t level;
    bool has_recording;
    bool busy;
} breathon_audio_status_t;

/* UI-only module. Call on the LVGL thread, or under the display lock. */
void breathon_ui_init(const lv_font_t *chinese_font);
void breathon_ui_show(breathon_page_t page);
void breathon_ui_set_network(bool known, bool connected);
/* Explicitly labelled local showcase fixtures; never sensor readings/history. */
void breathon_ui_set_demo(bool enabled);
bool breathon_ui_demo_enabled(void);
/* Presentation-only call scripts: no microphone, playback or network access. */
void breathon_ui_enable_voice_demo(void);
bool breathon_ui_voice_demo_enabled(void);
void breathon_ui_voice_demo_next(lv_event_t *event);
void breathon_ui_set_voice(const char *state, const char *transcript);
/* Nonblocking command sink on the LVGL thread: enqueue work, never wait for
 * network/audio. HOME click starts once; leaving VOICE stops once before exit.
 * Registration, show(VOICE), refresh and status publication never start.
 * Removing/replacing an active handler stops the old handler once; the new
 * handler waits for a new HOME click. NULL disables the HOME voice entry.
 * May publish status synchronously; must not recursively navigate/register.
 * Commands never imply connection state: publish it with set_voice(). */
void breathon_ui_set_voice_handler(breathon_voice_handler_t handler);
/* Handler owns five-second capture, volatile storage and playback. Keep it
 * nonblocking; publish status on the LVGL thread. NULL disables audio actions.
 * Leaving AUDIO sends STOP before navigation, even when reported idle. */
void breathon_ui_set_audio_handler(breathon_audio_handler_t handler);
/* Copies state text; level is 0..100 (clamped). NULL resets to idle/no recording.
 * A NULL state uses the idle/busy default. Status never triggers actions. */
void breathon_ui_set_audio(const breathon_audio_status_t *status);
breathon_page_t breathon_ui_page(void);
#endif
