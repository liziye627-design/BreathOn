#include "app_breathon_realtime_internal.h"
#include "app_breathon_audio.h"
#include "tal_network.h"
#include "cJSON.h"

static void elapsed_max(uint32_t *maximum, uint32_t start)
{
    uint32_t elapsed = tal_system_get_millisecond() - start;
    if (elapsed > *maximum) *maximum = elapsed;
}

static bool retryable(int e)
{
    return e == UNW_EAGAIN || e == UNW_EWOULDBLOCK || e == UNW_EINTR;
}
static bool end_reason(rt_session_t *s, const char *reason, int error)
{
    if (!s->end_reason) { s->end_reason = reason; s->end_errno = error; }
    return false;
}
/* Distinguish an exception bit from a failed syscall: successful T5 select
 * clears errno, so tal_net_get_errno() would report -100, not SO_ERROR. */
#define RT_SELECT_EXCEPTION (-2)
static int select_socket(int fd, bool write, bool *readable, bool *writable, int *error)
{
    TUYA_FD_SET_T reads, writes, errors;
    TAL_FD_ZERO(&reads); TAL_FD_ZERO(&writes); TAL_FD_ZERO(&errors);
    TAL_FD_SET(fd, &reads); TAL_FD_SET(fd, &errors);
    if (write) TAL_FD_SET(fd, &writes);
    /* T5 adapter interprets zero timeout as infinite, so always use >0. */
    int rc = tal_net_select(fd + 1, &reads, write ? &writes : NULL, &errors, 10);
    *error = rc < 0 ? tal_net_get_errno() : 0;
    *readable = rc > 0 && TAL_FD_ISSET(fd, &reads);
    *writable = rc > 0 && write && TAL_FD_ISSET(fd, &writes);
    return rc > 0 && TAL_FD_ISSET(fd, &errors) ? RT_SELECT_EXCEPTION : rc;
}
static bool connect_socket(rt_session_t *s, const rt_config_t *config)
{
    PR_NOTICE("BREATHON_REALTIME endpoint=%s:%u",config->host,(unsigned)config->port);
    s->socket = tal_net_socket_create(PROTOCOL_TCP);
    if (s->socket < 0) return end_reason(s, "socket_create", tal_net_get_errno());
    if (tal_net_set_block(s->socket, false) != OPRT_OK)
        return end_reason(s, "socket_nonblock", tal_net_get_errno());
    /* 20ms speech frames must not wait for Nagle/delayed-ACK batching. */
    if (tal_net_disable_nagle(s->socket) != OPRT_OK)
        return end_reason(s, "socket_nodelay", tal_net_get_errno());
    tal_net_set_timeout(s->socket, 100, TRANS_RECV);
    tal_net_set_timeout(s->socket, 100, TRANS_SEND);
    int rc = tal_net_connect(s->socket, tal_net_str2addr(config->host), config->port);
    if (rc == 0) return true;
    s->connect_errno = tal_net_get_errno();
    /* T5 does not map EINPROGRESS into a portable UNW constant. Poll boundedly;
     * the first nonblocking send verifies success/refusal without guessing errno. */
    uint32_t since = tal_system_get_millisecond();
    while (!rt_stop_requested() && (uint32_t)(tal_system_get_millisecond() - since) < 3000) {
        bool readable, writable;
        int error;
        rc = select_socket(s->socket, true, &readable, &writable, &error);
        if (rc == RT_SELECT_EXCEPTION) return end_reason(s, "connect_exception", 0);
        if (rc < 0 && !retryable(error)) return end_reason(s, "connect_select_error", error);
        if (rc > 0 && writable) return true;
    }
    return end_reason(s, rt_stop_requested() ? "local_stop" : "connect_timeout", 0);
}
static void queue_control(rt_session_t *s, const char *json)
{
    size_t n = strlen(json);
    rt_header_encode(s->tx, RT_CONTROL, (uint32_t)n, 0);
    memcpy(s->tx + RT_HEADER_SIZE, json, n);
    s->tx_used = RT_HEADER_SIZE + n; s->tx_sent = 0;
    s->tx_since = tal_system_get_millisecond();
}
static bool queue_hello(rt_session_t *s, const rt_config_t *config)
{
    cJSON *j = cJSON_CreateObject();
    if (!j) return false;
    bool ok = cJSON_AddStringToObject(j, "type", "hello") &&
              cJSON_AddStringToObject(j, "token", config->token) &&
              cJSON_AddStringToObject(j, "device_id", "nicemcu-01") &&
              cJSON_AddNumberToObject(j, "sample_rate", 16000);
    char *json = ok ? cJSON_PrintUnformatted(j) : NULL;
    ok = json && strlen(json) <= RT_FRAME_BYTES;
    if (ok) queue_control(s, json);
    if (json) { memset(json, 0, strlen(json)); cJSON_free(json); }
    cJSON_Delete(j);
    return ok;
}
static bool send_pending(rt_session_t *s)
{
    uint32_t before = tal_system_get_millisecond();
    int sent = tal_net_send(s->socket, s->tx + s->tx_sent, (uint32_t)(s->tx_used - s->tx_sent));
    int error = sent < 0 ? tal_net_get_errno() : 0;
    elapsed_max(&s->send_ms_max, before);
    elapsed_max(&s->tx_pending_age_ms_max, s->tx_since);
    if (sent < 0) {
        if (retryable(error)) {
            if (s->tx_retryable_send_count < UINT32_MAX) ++s->tx_retryable_send_count;
            return true;
        }
        return end_reason(s, "send_error", error);
    }
    if (!sent || (size_t)sent > s->tx_used - s->tx_sent) return end_reason(s, "send_count", 0);
    rt_header_t header;
    if (rt_header_decode(&header, s->tx) && header.kind == RT_MIC) {
        size_t before = s->tx_sent > RT_HEADER_SIZE ? s->tx_sent - RT_HEADER_SIZE : 0;
        size_t after = s->tx_sent + (size_t)sent > RT_HEADER_SIZE ? s->tx_sent + (size_t)sent - RT_HEADER_SIZE : 0;
        s->mic_tx_bytes += (uint32_t)(after - before);
    }
    s->tx_sent += (size_t)sent;
    if (s->tx_sent == s->tx_used) {
        memset(s->tx, 0, s->tx_used); s->tx_sent = s->tx_used = 0;
    }
    return true;
}
static const char *session_failure(rt_session_t *s)
{
    uint32_t irq = tal_system_enter_critical();
    const char *failure = s->mic_overflow ? "麦克风队列溢出" : s->failure;
    tal_system_exit_critical(irq);
    return failure;
}
static bool receive_counted(void *arg, const rt_header_t *header, const uint8_t *data)
{
    rt_session_t *s = arg;
    uint32_t irq = tal_system_enter_critical();
    bool accepted = header->kind == RT_SPEAKER && s->ready &&
                    rt_generation_accept(&s->generation, header->generation);
    tal_system_exit_critical(irq);
    /* Only this worker changes generation; a stale frame may return true from
     * rt_receive_frame while being deliberately discarded, so exclude it. */
    bool ok = rt_receive_frame(arg, header, data);
    if (ok && accepted) s->speaker_rx_bytes += header->length;
    return ok;
}
static const char *failure_code(const char *failure)
{
    static const struct { const char *text, *code; } codes[] = {
        {"麦克风队列溢出", "mic_overflow"}, {"播放队列溢出", "speaker_overflow"},
        {"音频播放失败", "playback_error"}, {"麦克风启动失败", "mic_start_error"},
        {"网关会话失败", "gateway_error"}, {"网络已断开", "wifi_down"},
        {"网关响应超时", "response_timeout"}, {"网关发送超时", "send_timeout"},
        {"网关协议错误", "protocol_error"}, {"握手准备失败", "hello_prepare"}
    };
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i)
        if (!strcmp(failure, codes[i].text)) return codes[i].code;
    return "session_failure"; /* Never log arbitrary failure/control text. */
}
static void run_session(rt_session_t *s)
{
    uint32_t previous = tal_system_get_millisecond();
    while (!rt_stop_requested()) {
        elapsed_max(&s->loop_ms_max, previous);
        previous = tal_system_get_millisecond();
        if (s->tx_used) elapsed_max(&s->tx_pending_age_ms_max, s->tx_since);
        rt_publish_pending();
        const char *failure = session_failure(s);
        if (failure) { rt_fail(s, failure); return; }
        uint32_t now = tal_system_get_millisecond();
        if (!rt_wifi_up()) { rt_fail(s, "网络已断开"); return; }
        if ((uint32_t)(now - s->start_ms) > RT_SESSION_MS) { end_reason(s, "session_limit", 0); s->closed = true; return; }
        if ((!s->ready && (uint32_t)(now - s->start_ms) > 15000) ||
            (uint32_t)(now - s->last_rx) > 15000) { rt_fail(s, "网关响应超时"); return; }
        if (s->tx_used && (uint32_t)(now - s->tx_since) > 2000) { rt_fail(s, "网关发送超时"); return; }
        if (!s->tx_used) {
            if ((uint32_t)(now - s->last_ping) >= 5000) {
                queue_control(s, "{\"type\":\"ping\"}"); s->last_ping = now;
            } else if (s->ready) {
                uint32_t irq = tal_system_enter_critical();
                /* Keep 20ms latency when current; after a delayed poll drain
                 * all whole frames that fit the existing wire payload limit. */
                size_t available = s->mic.used / RT_FRAME_BYTES * RT_FRAME_BYTES;
                size_t maximum = RT_PAYLOAD_MAX / RT_FRAME_BYTES * RT_FRAME_BYTES;
                size_t n = rt_ring_read(&s->mic, s->tx + RT_HEADER_SIZE,
                                        available < maximum ? available : maximum);
                tal_system_exit_critical(irq);
                if (n) {
                    rt_header_encode(s->tx, RT_MIC, (uint32_t)n, 0);
                    s->tx_used = RT_HEADER_SIZE + n; s->tx_since = now;
                    if (n > s->mic_tx_batch_max) s->mic_tx_batch_max = (uint32_t)n;
                }
            }
        }
        bool readable, writable;
        int error;
        uint32_t before = tal_system_get_millisecond();
        int rc = select_socket(s->socket, s->tx_used != 0, &readable, &writable, &error);
        elapsed_max(&s->select_ms_max, before);
        if (s->tx_used) elapsed_max(&s->tx_pending_age_ms_max, s->tx_since);
        if (rc < 0) {
            if (rc != RT_SELECT_EXCEPTION && retryable(error)) continue;
            end_reason(s, rc == RT_SELECT_EXCEPTION ? "select_exception" : "select_error", error);
            rt_fail(s, "网关连接中断"); return;
        }
        /* Count successful polls lacking write readiness only while TX is pending. */
        if (s->tx_used && !writable && s->tx_no_writable_count < UINT32_MAX)
            ++s->tx_no_writable_count;
        /* Receive controls before sending more mic data. TCP fragmentation and
         * coalescing are handled by the incremental fixed-capacity parser. */
        if (readable) {
            before = tal_system_get_millisecond();
            int n = tal_net_recv(s->socket, s->rx, sizeof(s->rx));
            error = n < 0 ? tal_net_get_errno() : 0;
            elapsed_max(&s->recv_ms_max, before);
            if (!n) { end_reason(s, "peer_eof", 0); rt_fail(s, "网关连接已关闭"); return; }
            if (n < 0) {
                if (!retryable(error)) { end_reason(s, "recv_error", error); rt_fail(s, "网关接收失败"); return; }
            }
            else {
                s->last_rx = now;
                before = tal_system_get_millisecond();
                bool accepted = rt_parser_feed(&s->parser, s->rx, (size_t)n, receive_counted, s);
                elapsed_max(&s->control_ms_max, before);
                if (!accepted) {
                    if (!s->closed && !session_failure(s)) rt_fail(s, "网关协议错误");
                    return;
                }
            }
        }
        if (writable && s->tx_used && !send_pending(s)) { rt_fail(s, "网关发送失败"); return; }
    }
}
static void best_effort_stop(rt_session_t *s)
{
    /* Never splice STOP into a partially sent frame; closing TCP already tells
     * the gateway to cancel upstream. No reconnect/retry of a paid session. */
    if (s->socket < 0 || s->tx_sent) return;
    queue_control(s, "{\"type\":\"stop\"}");
    uint32_t since = tal_system_get_millisecond();
    while (s->tx_used && (uint32_t)(tal_system_get_millisecond() - since) < 100) {
        bool read, write;
        int error;
        if (select_socket(s->socket, true, &read, &write, &error) < 0) break;
        if (write && !send_pending(s)) break;
    }
}
void rt_network_session(const rt_config_t *config)
{
    if (!app_breathon_audio_acquire(BREATHON_OWNER_REALTIME)) {
        app_breathon_realtime_publish("error", "音频正在使用中"); return;
    }
    rt_session_t *s = Malloc(sizeof(*s));
    if (!s) {
        app_breathon_audio_release(BREATHON_OWNER_REALTIME);
        app_breathon_realtime_publish("error", "音频缓冲分配失败"); return;
    }
    memset(s, 0, sizeof(*s)); s->socket = -1;
    rt_ring_init(&s->mic, s->mic_storage, sizeof(s->mic_storage));
    rt_ring_init(&s->speaker, s->speaker_storage, sizeof(s->speaker_storage));
    uint32_t irq = tal_system_enter_critical();
    rt_current = s; rt_playback_idle = false;
    tal_system_exit_critical(irq);
    app_breathon_realtime_publish("connecting", "正在连接");
    if (!connect_socket(s, config)) rt_fail(s, "无法连接网关");
    else if (!queue_hello(s, config)) rt_fail(s, "握手准备失败");
    else {
        s->start_ms = s->last_rx = s->last_ping = tal_system_get_millisecond();
        run_session(s);
    }
    /* Include the last receive/control/failure delay before cleanup replaces TX. */
    if (s->tx_used) elapsed_max(&s->tx_pending_age_ms_max, s->tx_since);
    irq = tal_system_enter_critical();
    s->capture_on = false; s->generation.open = false;
    rt_ring_clear(&s->mic); rt_ring_clear(&s->speaker); rt_current = NULL;
    tal_system_exit_critical(irq);
    const char *failure = session_failure(s);
    const char *reason = s->end_reason ? s->end_reason : failure ? failure_code(failure) :
                         s->closed ? "gateway_closed" : rt_stop_requested() ? "local_stop" : "session_end";
    PR_NOTICE("BREATHON_REALTIME end reason=%s errno=%d connect_errno=%d ready=%d mic_tx_bytes=%u speaker_rx_bytes=%u select_ms=%u recv_ms=%u control_ms=%u send_ms=%u loop_ms=%u mic_highwater=%u mic_frame=%u tx_pending_age_ms=%u tx_no_writable_count=%u tx_retryable_send_count=%u mic_tx_batch_max=%u",
              reason, s->end_errno, s->connect_errno, s->ready,
              (unsigned)s->mic_tx_bytes, (unsigned)s->speaker_rx_bytes,
              (unsigned)s->select_ms_max, (unsigned)s->recv_ms_max, (unsigned)s->control_ms_max,
              (unsigned)s->send_ms_max, (unsigned)s->loop_ms_max,
              (unsigned)s->mic_highwater, (unsigned)s->mic_frame_max,
              (unsigned)s->tx_pending_age_ms_max, (unsigned)s->tx_no_writable_count,
              (unsigned)s->tx_retryable_send_count, (unsigned)s->mic_tx_batch_max);
    best_effort_stop(s);
    if (s->socket >= 0) tal_net_close(s->socket);
    ai_audio_player_stop(AI_AUDIO_PLAYER_ALL);
    /* Wait for an in-flight SDK START/feed to return and be stopped again before
     * freeing buffers or returning ownership to offline/cloud users. */
    for (;;) {
        irq = tal_system_enter_critical(); bool idle = rt_playback_idle; tal_system_exit_critical(irq);
        if (idle) break;
        tal_system_sleep(5);
    }
    failure = s->failure;
    memset(s, 0, sizeof(*s)); Free(s);
    app_breathon_audio_release(BREATHON_OWNER_REALTIME);
    if (failure && !rt_stop_requested()) app_breathon_realtime_publish("error", failure);
    else app_breathon_realtime_publish("stopped", "已停止");
}
