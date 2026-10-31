#ifndef APP_BREATHON_REALTIME_INTERNAL_H
#define APP_BREATHON_REALTIME_INTERNAL_H
#include "app_breathon_realtime.h"
#include "realtime_wire.h"
#include "ai_audio_player.h"

#define RT_TOKEN_MAX 128u
/* 320ms at 16kHz/s16 absorbs observed 180ms WiFi TX stalls. Overflow still
 * closes capture; the separate wire batch remains capped at 120ms of PCM. */
#define RT_MIC_CAPACITY (16u * RT_FRAME_BYTES)
#define RT_SPEAKER_CAPACITY (16u * RT_FRAME_BYTES)
#define RT_SESSION_MS 180000u
typedef struct { char host[16], token[RT_TOKEN_MAX + 1]; uint16_t port; } rt_config_t;
typedef struct {
    rt_parser_t parser;
    rt_ring_t mic, speaker;
    rt_generation_t generation;
    uint8_t mic_storage[RT_MIC_CAPACITY], speaker_storage[RT_SPEAKER_CAPACITY];
    uint8_t tx[RT_HEADER_SIZE + RT_PAYLOAD_MAX], rx[1024];
    size_t tx_used, tx_sent;
    uint32_t tx_since, last_rx, last_ping, start_ms;
    /* Network-worker diagnostics: accepted payload bytes, never heard/DAC progress. */
    uint32_t mic_tx_bytes, speaker_rx_bytes;
    uint32_t mic_highwater, mic_frame_max;
    uint32_t select_ms_max, recv_ms_max, control_ms_max, send_ms_max, loop_ms_max;
    /* Worker-only TX pressure: queue-to-completion age, saturating poll/retry counts. */
    uint32_t tx_pending_age_ms_max, tx_no_writable_count, tx_retryable_send_count;
    uint32_t mic_tx_batch_max; /* Largest queued MIC payload, excluding wire header. */
    const char *end_reason;
    int end_errno, connect_errno;
    int socket;
    bool ready, capture_on, mic_overflow, closed;
    const char *failure;
} rt_session_t;

/* Every access to shared session fields/pointer is inside a TAL critical section.
 * Network-only parser/socket/tx/timers need no lock. Never hold a critical section
 * while invoking the SDK player, network, allocator, logger or UI. */
extern rt_session_t *rt_current;
extern bool rt_playback_idle;
extern int rt_request; /* -1 configuring, 0 idle, 1 requested, 2 running, 3 stopping */
void rt_publish_pending(void);
static inline void rt_fail(rt_session_t *s, const char *text)
{
    uint32_t irq = tal_system_enter_critical();
    if (!s->failure) s->failure = text;
    tal_system_exit_critical(irq);
}
bool rt_stop_requested(void);
bool rt_wifi_up(void);
void rt_playback_task(void *arg);
void rt_network_session(const rt_config_t *config);
bool rt_receive_frame(void *arg, const rt_header_t *header, const uint8_t *data);
OPERATE_RET rt_cli_register(void);
#endif
