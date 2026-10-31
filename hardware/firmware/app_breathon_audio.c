/* Offline diagnostics reuse the running codec; the IPC callback never logs or blocks. */
#include "app_breathon_audio.h"
#include "ai_audio_input.h"
#include "ai_audio_player.h"
#include "tkl_kws.h"
#include "tuya_ai_agent.h"
extern bool app_breathon_realtime_configured(void) __attribute__((weak));

static bool openai_selected(void)
{
    return app_breathon_realtime_configured && app_breathon_realtime_configured();
}

typedef enum { LOCAL_IDLE, LOCAL_RECORD, LOCAL_READY, LOCAL_PLAY, LOCAL_ERROR } local_state_t;
static audio_capture_t capture;
static local_state_t state;
static int pending = -1;
static int audio_owner;
static THREAD_HANDLE task;
static uint32_t started_ms, play_offset, play_size;
static const char *error_text = "录音失败";

bool ai_audio_local_capture_active(void)
{
    return app_breathon_audio_owner() != BREATHON_OWNER_NONE || openai_selected();
}

breathon_audio_owner_t app_breathon_audio_owner(void)
{
    return (breathon_audio_owner_t)__atomic_load_n(&audio_owner, __ATOMIC_ACQUIRE);
}

void app_breathon_audio_request(int action)
{
    if (action < 0 || action > BREATHON_AUDIO_STATUS) return;
    /* Stop has priority; other commands cannot overwrite an unhandled stop. */
    uint32_t irq = tal_system_enter_critical();
    if (action == BREATHON_AUDIO_STOP || __atomic_load_n(&pending, __ATOMIC_RELAXED) == -1)
        __atomic_store_n(&pending, action, __ATOMIC_RELEASE);
    tal_system_exit_critical(irq);
}

void app_breathon_audio_action(breathon_audio_action_t action)
{
    app_breathon_audio_request((int)action);
}

bool app_breathon_audio_stop_pending(void)
{
    return __atomic_load_n(&pending, __ATOMIC_ACQUIRE) == BREATHON_AUDIO_STOP;
}

void app_breathon_audio_event(AI_NOTIFY_EVENT_T *event)
{
    if (event->type != AI_USER_EVT_MIC_DATA || capture.wav == NULL) return;
    AI_NOTIFY_MIC_DATA_T *mic = event->data;
    if (mic == NULL || mic->data == NULL) return;
    uint32_t irq = tal_system_enter_critical();
    if (state == LOCAL_RECORD) {
        if (mic->data_len > 4096 || !audio_capture_append(&capture, mic->data, mic->data_len)) {
            error_text = "录音格式异常";
            state = LOCAL_ERROR;
        }
    }
    tal_system_exit_critical(irq);
}

static audio_capture_t snapshot(void)
{
    uint32_t irq = tal_system_enter_critical();
    audio_capture_t result = capture;
    tal_system_exit_critical(irq);
    return result;
}

static void set_state(local_state_t next)
{
    uint32_t irq = tal_system_enter_critical();
    state = next;
    tal_system_exit_critical(irq);
}

bool app_breathon_audio_acquire(breathon_audio_owner_t owner)
{
    if (owner != BREATHON_OWNER_LOCAL && owner != BREATHON_OWNER_REALTIME) return false;
    uint32_t irq = tal_system_enter_critical();
    bool acquired = __atomic_load_n(&audio_owner, __ATOMIC_RELAXED) == BREATHON_OWNER_NONE;
    if (acquired) __atomic_store_n(&audio_owner, owner, __ATOMIC_RELEASE);
    tal_system_exit_critical(irq);
    if (!acquired) return false;
    /* Best effort: an offline device has no cloud session to cancel. */
    (void)tuya_ai_agent_event(AI_EVENT_CHAT_BREAK, 0);
    tkl_kws_disable();
    ai_user_event_notify(AI_USER_EVT_EXIT, NULL);
    ai_audio_input_wakeup_set(false);
    ai_audio_player_stop(AI_AUDIO_PLAYER_ALL);
    return true;
}

void app_breathon_audio_release(breathon_audio_owner_t owner)
{
    if (owner == BREATHON_OWNER_NONE || app_breathon_audio_owner() != owner) return;
    ai_audio_player_stop(AI_AUDIO_PLAYER_ALL);
    ai_user_event_notify(AI_USER_EVT_EXIT, NULL);
    ai_audio_input_wakeup_set(false);
    if (!openai_selected()) tkl_kws_enable();
    __atomic_store_n(&audio_owner, BREATHON_OWNER_NONE, __ATOMIC_RELEASE);
}

static void stop_local(void)
{
    if (app_breathon_audio_owner() != BREATHON_OWNER_LOCAL) return;
    uint32_t irq = tal_system_enter_critical();
    if (state == LOCAL_RECORD) audio_capture_reset(&capture, capture.wav);
    state = capture.bytes ? LOCAL_READY : LOCAL_IDLE;
    tal_system_exit_critical(irq);
    app_breathon_audio_release(BREATHON_OWNER_LOCAL);
    PR_NOTICE("BREATHON_AUDIO stop samples=%u", capture.bytes / 2);
}

static void report(void)
{
    audio_capture_t value = snapshot();
    PR_NOTICE("BREATHON_AUDIO state=%d bytes=%u samples=%u peak=%u clipped=%u mean_square=%u local=%d",
              state, value.bytes, value.bytes / 2, value.peak, value.clipped,
              value.bytes ? (uint32_t)(value.energy / (value.bytes / 2)) : 0,
              ai_audio_local_capture_active());
}

static void process_command(int command)
{
    if (command == BREATHON_AUDIO_STOP) { stop_local(); return; }
    if (command == BREATHON_AUDIO_STATUS) { report(); return; }
    if (app_breathon_audio_owner() == BREATHON_OWNER_REALTIME) return;
    if (state == LOCAL_RECORD || state == LOCAL_PLAY) return;
    switch (command) {
    case BREATHON_AUDIO_RECORD: {
        if (!app_breathon_audio_acquire(BREATHON_OWNER_LOCAL)) return;
        if (ai_audio_input_start() != OPRT_OK) {
            error_text = "麦克风启动失败"; set_state(LOCAL_ERROR); break;
        }
        uint32_t irq = tal_system_enter_critical();
        audio_capture_reset(&capture, capture.wav);
        started_ms = tal_system_get_millisecond();
        state = LOCAL_RECORD;
        tal_system_exit_critical(irq);
        PR_NOTICE("BREATHON_AUDIO record rate=16000 bits=16 channels=1 duration_ms=5000");
        break;
    }
    case BREATHON_AUDIO_PLAY:
        if (capture.bytes == 0) break;
        if (!app_breathon_audio_acquire(BREATHON_OWNER_LOCAL)) return;
        audio_capture_finish(&capture);
        play_offset = 0; play_size = AUDIO_WAV_HEADER + capture.bytes;
        set_state(LOCAL_PLAY);
        if (ai_audio_play_tts_stream(AI_AUDIO_PLAYER_TTS_START, AI_AUDIO_CODEC_WAV, NULL, 0) != OPRT_OK) {
            error_text = "回放启动失败"; set_state(LOCAL_ERROR);
        }
        started_ms = tal_system_get_millisecond();
        PR_NOTICE("BREATHON_AUDIO replay bytes=%u", play_size);
        break;
    case BREATHON_AUDIO_TONE:
        if (!app_breathon_audio_acquire(BREATHON_OWNER_LOCAL)) return;
        play_size = play_offset = 0;
        set_state(LOCAL_PLAY);
        if (ai_audio_player_alert(AI_AUDIO_ALERT_WAKEUP) != OPRT_OK) {
            error_text = "提示音播放失败"; set_state(LOCAL_ERROR);
        }
        started_ms = tal_system_get_millisecond();
        PR_NOTICE("BREATHON_AUDIO local_tone");
        break;
    case BREATHON_AUDIO_EXPORT:
        if (capture.bytes != 0) { audio_capture_finish(&capture); app_breathon_audio_export(&capture); }
        break;
    default: break;
    }
}

static void tick(void)
{
    static uint32_t published_ms;
    audio_capture_t value = snapshot();
    uint32_t elapsed = (uint32_t)tal_system_get_millisecond() - started_ms;
    if (state == LOCAL_RECORD && value.bytes == AUDIO_PCM_BYTES) {
        set_state(LOCAL_READY);
        audio_capture_finish(&capture);
        report();
    } else if (state == LOCAL_RECORD && elapsed > 8000) {
        error_text = "录音超时，请重试"; set_state(LOCAL_ERROR); report();
    }
    if (state == LOCAL_PLAY) {
        if (play_offset < play_size) {
            uint32_t count = play_size - play_offset;
            if (count > 640) count = 640;
            OPERATE_RET result = ai_audio_play_tts_stream(AI_AUDIO_PLAYER_TTS_DATA, AI_AUDIO_CODEC_WAV,
                                           (char *)capture.wav + play_offset, count);
            if (result != OPRT_OK) { error_text = "回放数据失败"; set_state(LOCAL_ERROR); }
            play_offset += count;
            if (play_offset == play_size)
                ai_audio_play_tts_stream(AI_AUDIO_PLAYER_TTS_STOP, AI_AUDIO_CODEC_WAV, NULL, 0);
        } else if (elapsed > 250 && !ai_audio_player_is_playing()) {
            set_state(value.bytes ? LOCAL_READY : LOCAL_IDLE);
            PR_NOTICE("BREATHON_AUDIO playback_finished");
        }
        if (elapsed > 15000) {
            ai_audio_player_stop(AI_AUDIO_PLAYER_ALL);
            error_text = "回放超时"; set_state(LOCAL_ERROR);
        }
    }
    if (state != LOCAL_RECORD && state != LOCAL_PLAY &&
        app_breathon_audio_owner() == BREATHON_OWNER_LOCAL)
        app_breathon_audio_release(BREATHON_OWNER_LOCAL);
    uint32_t now = tal_system_get_millisecond();
    if ((uint32_t)(now - published_ms) < 100) return;
    published_ms = now;
    const char *labels[] = {"准备录音", "正在录音", "录音完成", "正在回放"};
    breathon_audio_status_t status = {
        .state = state == LOCAL_ERROR ? error_text : labels[state],
        .elapsed_ms = value.bytes / 32,
        .level = state == LOCAL_RECORD ? (value.frame_mean > 6000 ? 100 : value.frame_mean / 60) : 0,
        .has_recording = value.bytes > 0 && state != LOCAL_RECORD && state != LOCAL_ERROR,
        .busy = state == LOCAL_RECORD || state == LOCAL_PLAY,
    };
    app_breathon_audio_publish(&status);
}

static void audio_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* Use the SDK's dedicated SMP lock, not exclusives on ordinary SRAM. */
        uint32_t irq = tal_system_enter_critical();
        int command = __atomic_load_n(&pending, __ATOMIC_RELAXED);
        __atomic_store_n(&pending, -1, __ATOMIC_RELEASE);
        tal_system_exit_critical(irq);
        if (command >= 0) process_command(command);
        tick();
        tal_system_sleep(20);
    }
}

OPERATE_RET app_breathon_audio_init(void)
{
    capture.wav = Malloc(AUDIO_WAV_HEADER + AUDIO_PCM_BYTES);
    if (capture.wav == NULL) return OPRT_MALLOC_FAILED;
    audio_capture_reset(&capture, capture.wav);
    THREAD_CFG_T cfg = {.thrdname = "breath_audio", .priority = THREAD_PRIO_2, .stackDepth = 4096};
    OPERATE_RET result = tal_thread_create_and_start(&task, NULL, NULL, audio_task, NULL, &cfg);
    if (result != OPRT_OK) { Free(capture.wav); capture.wav = NULL; return result; }
    result = app_breathon_audio_cli_register();
    PR_NOTICE("BREATHON_AUDIO ready firmware=%s rate=16000 bits=16 channels=1", PROJECT_VERSION);
    return result;
}
