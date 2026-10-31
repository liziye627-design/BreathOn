/* Deterministic fault injection: actual network/control/parser code, no sockets. */
#include "tal_network.h"
#include "app_breathon_realtime_internal.h"
#include <assert.h>
#include <stdarg.h>
/* Fail without invoking the Windows CRT abort dialog. */
#undef assert
#define assert(ok) do { if (!(ok)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #ok); exit(1); } } while (0)

static char summary[512];
static unsigned log_count, critical_depth;
void capture_log(const char *fmt, ...)
{
    assert(!critical_depth);
    /* The endpoint notice precedes the one terminal diagnostic summary. */
    if (!strcmp(fmt, "BREATHON_REALTIME endpoint=%s:%u")) return;
    va_list args; va_start(args, fmt);
    int n = vsnprintf(summary, sizeof(summary), fmt, args); va_end(args);
    assert(n >= 0 && (size_t)n < sizeof(summary));
    ++log_count;
}
#undef PR_NOTICE
#define PR_NOTICE(...) capture_log(__VA_ARGS__)
#include "../app_breathon_realtime_network.c"

rt_session_t *rt_current;
bool rt_playback_idle;
int rt_request;
static uint32_t clock_ms;
static int net_errno, connect_result, connect_error, select_result, select_error;
static int recv_result, recv_error, send_error, send_limit;
static bool exception_ready, stopped, wifi_up;
static unsigned receive_calls, select_calls;
static unsigned send_calls, no_write_polls, stop_after_polls;
static uint32_t send_delay;
static int forced_send_result;
static unsigned nagle_calls, player_stop_delay;
static const char *injected_failure;
static uint8_t inbound[1024];
static size_t inbound_size;
static uint8_t outbound[RT_HEADER_SIZE + RT_PAYLOAD_MAX];
static size_t outbound_size;
static bool stop_after_send;

uint32_t tal_system_enter_critical(void) { return critical_depth++; }
void tal_system_exit_critical(uint32_t saved) { assert(critical_depth == saved + 1); --critical_depth; }
uint32_t tal_system_get_millisecond(void) { return clock_ms; }
void tal_system_sleep(uint32_t ms) { clock_ms += ms; }
bool rt_stop_requested(void) { return stopped; }
bool rt_wifi_up(void) { return wifi_up; }
void rt_publish_pending(void) { if (injected_failure) rt_fail(rt_current, injected_failure); }
bool app_breathon_audio_acquire(breathon_audio_owner_t owner) { (void)owner; return true; }
void app_breathon_audio_release(breathon_audio_owner_t owner) { (void)owner; }
void app_breathon_realtime_publish(const char *state, const char *text) { (void)state; (void)text; }
OPERATE_RET ai_audio_player_stop(int type) { (void)type; clock_ms += player_stop_delay; rt_playback_idle = true; return 0; }
OPERATE_RET ai_audio_input_start(void) { return 0; }
int tal_net_get_errno(void) { return net_errno; }
int tal_net_socket_create(int type) { (void)type; return 4; }
int tal_net_set_block(int fd, bool block) { (void)fd; assert(!block); return 0; }
int tal_net_disable_nagle(int fd) { (void)fd; ++nagle_calls; return 0; }
int tal_net_set_timeout(int fd, int ms, int type) { (void)fd; (void)ms; (void)type; return 0; }
TUYA_IP_ADDR_T tal_net_str2addr(const char *host) { (void)host; return 0; }
int tal_net_connect(int fd, TUYA_IP_ADDR_T ip, uint16_t port)
{ (void)fd; (void)ip; (void)port; assert(nagle_calls == 1); net_errno = connect_error; return connect_result; }
int tal_net_close(int fd) { (void)fd; net_errno = -99; return 0; }
int tal_net_select(int fd, TUYA_FD_SET_T *r, TUYA_FD_SET_T *w, TUYA_FD_SET_T *e, uint32_t ms)
{
    assert(fd == 5 && ms > 0); assert(++select_calls < 2000); clock_ms += ms;
    if (stop_after_polls && select_calls == stop_after_polls) stopped = true;
    TAL_FD_ZERO(r); TAL_FD_ZERO(e);
    if (w) TAL_FD_ZERO(w);
    if (select_result > 0) {
        if (exception_ready) TAL_FD_SET(4, e);
        else { TAL_FD_SET(4, r); if (w && select_calls > no_write_polls) TAL_FD_SET(4, w); }
    }
    net_errno = select_error;
    return select_result;
}
int tal_net_send(int fd, const void *data, uint32_t n)
{
    (void)fd; (void)data;
    ++send_calls; clock_ms += send_delay;
    if (send_error) { net_errno = send_error; return -1; }
    if (forced_send_result >= 0) return forced_send_result;
    if (stop_after_send) {
        assert(n <= sizeof(outbound));
        memcpy(outbound, data, n); outbound_size = n; stopped = true;
    }
    return send_limit && n > (uint32_t)send_limit ? send_limit : (int)n;
}
int tal_net_recv(int fd, void *data, uint32_t n)
{
    (void)fd; ++receive_calls;
    if (inbound_size) {
        assert(inbound_size <= n); memcpy(data, inbound, inbound_size);
        int size = (int)inbound_size; inbound_size = 0; return size;
    }
    net_errno = recv_error; return recv_result;
}
static void reset(void)
{
    memset(summary, 0, sizeof(summary)); log_count = critical_depth = 0;
    clock_ms = 0; net_errno = connect_result = connect_error = 0;
    select_result = 1; select_error = -100; recv_result = 0;
    recv_error = -20; send_error = send_limit = 0;
    exception_ready = stopped = false; wifi_up = true;
    receive_calls = select_calls = 0; inbound_size = 0;
    send_calls = no_write_polls = stop_after_polls = 0;
    send_delay = 0; forced_send_result = -1;
    injected_failure = NULL;
    nagle_calls = player_stop_delay = 0;
    stop_after_send = false; outbound_size = 0;
}
static void session(void)
{
    const rt_config_t config = {"192.0.2.1", "diagnostic-secret-canary", 1234};
    rt_network_session(&config);
    assert(log_count == 1);
    assert(strstr(summary, "diagnostic-secret-canary") == NULL);
    assert(strstr(summary, "192.0.2.1") == NULL);
    assert(rt_current == NULL && critical_depth == 0);
}
static void append_frame(uint8_t kind, uint32_t generation, const void *data, size_t size)
{
    assert(inbound_size + RT_HEADER_SIZE + size <= sizeof(inbound));
    assert(rt_header_encode(inbound + inbound_size, kind, (uint32_t)size, generation));
    memcpy(inbound + inbound_size + RT_HEADER_SIZE, data, size);
    inbound_size += RT_HEADER_SIZE + size;
}
static void test_payload_counts(void)
{
    rt_session_t s = {0}; s.socket = 4;
    assert(rt_header_encode(s.tx, RT_MIC, RT_FRAME_BYTES, 0));
    s.tx_used = RT_HEADER_SIZE + RT_FRAME_BYTES;
    send_limit = 8;
    assert(send_pending(&s) && s.mic_tx_bytes == 0); /* header only */
    assert(send_pending(&s) && s.mic_tx_bytes == 4); /* header boundary */
    send_error = UNW_EAGAIN;
    assert(send_pending(&s) && s.mic_tx_bytes == 4 && !s.end_reason);
    send_error = 0;
    while (s.tx_used) assert(send_pending(&s));
    assert(s.mic_tx_bytes == RT_FRAME_BYTES);
    queue_control(&s, "{\"type\":\"ping\"}");
    while (s.tx_used) assert(send_pending(&s));
    assert(s.mic_tx_bytes == RT_FRAME_BYTES); /* controls excluded */
    queue_control(&s, "{\"type\":\"ping\"}"); send_error = -20;
    assert(!send_pending(&s) && s.end_errno == -20 && !strcmp(s.end_reason, "send_error"));

    rt_ring_init(&s.speaker, s.speaker_storage, sizeof(s.speaker_storage));
    s.ready = true; assert(rt_generation_start(&s.generation, 2));
    uint8_t pcm[RT_FRAME_BYTES] = {0};
    rt_header_t h = {.kind = RT_SPEAKER, .length = sizeof(pcm), .generation = 2};
    assert(receive_counted(&s, &h, pcm) && s.speaker_rx_bytes == sizeof(pcm));
    h.generation = 1;
    assert(receive_counted(&s, &h, pcm) && s.speaker_rx_bytes == sizeof(pcm));
    h.generation = 3;
    assert(!receive_counted(&s, &h, pcm) && s.speaker_rx_bytes == sizeof(pcm));
    h.generation = 2; s.speaker.used = s.speaker.capacity;
    assert(!receive_counted(&s, &h, pcm) && s.speaker_rx_bytes == sizeof(pcm));
}
static void test_microphone_catches_up_after_scheduler_delay(size_t queued)
{
    /* Six 20ms callbacks can arrive before the worker drains its backlog.
     * One writable poll must catch up, preserving every sample in order. */
    rt_session_t s = {0};
    s.socket = 4; s.ready = true; rt_current = &s;
    rt_ring_init(&s.mic, s.mic_storage, sizeof(s.mic_storage));
    uint8_t pcm[RT_MIC_CAPACITY];
    for (size_t i = 0; i < sizeof(pcm); ++i) pcm[i] = (uint8_t)i;
    assert(rt_ring_write(&s.mic, pcm, queued));
    size_t maximum = RT_PAYLOAD_MAX / RT_FRAME_BYTES * RT_FRAME_BYTES;
    size_t batch = queued < maximum ? queued : maximum;
    recv_result = -1; recv_error = UNW_EAGAIN; stop_after_send = true;
    run_session(&s);
    rt_header_t header;
    assert(rt_header_decode(&header, outbound));
    assert(header.kind == RT_MIC && header.length == batch);
    assert(outbound_size == RT_HEADER_SIZE + batch);
    assert(!memcmp(outbound + RT_HEADER_SIZE, pcm, batch));
    assert(s.mic.used == queued - batch && select_calls == 1 && s.mic_tx_bytes == batch);
    assert(s.mic_tx_batch_max == batch && s.tx_pending_age_ms_max == 10);
    assert(s.tx_no_writable_count == 0 && s.tx_retryable_send_count == 0);
    size_t sent = batch;
    while (s.mic.used) {
        size_t next = s.mic.used < maximum ? s.mic.used : maximum;
        stopped = false; run_session(&s);
        assert(rt_header_decode(&header, outbound) && header.length == next);
        assert(!memcmp(outbound + RT_HEADER_SIZE, pcm + sent, next));
        sent += next;
        assert(s.mic.used == queued - sent && s.mic_tx_bytes == sent && s.mic_tx_batch_max == batch);
    }
    rt_current = NULL;
}
static void test_pending_age_survives_partial_sends_retries_and_clock_wrap(void)
{
    rt_session_t s = {0}; s.socket = 4;
    clock_ms = UINT32_MAX - 19; s.tx_since = clock_ms;
    assert(rt_header_encode(s.tx, RT_MIC, RT_FRAME_BYTES, 0));
    memset(s.tx + RT_HEADER_SIZE, 0x5a, RT_FRAME_BYTES);
    s.tx_used = RT_HEADER_SIZE + RT_FRAME_BYTES;
    uint8_t original[RT_HEADER_SIZE + RT_FRAME_BYTES];
    memcpy(original, s.tx, sizeof(original));
    send_limit = 8;
    clock_ms += 10; assert(send_pending(&s) && s.tx_sent == 8 && s.mic_tx_bytes == 0);
    clock_ms += 10; assert(send_pending(&s) && s.tx_sent == 16 && s.mic_tx_bytes == 4);
    assert(s.tx_pending_age_ms_max == 20);
    const int errors[] = {UNW_EAGAIN, UNW_EWOULDBLOCK, UNW_EINTR};
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        clock_ms += 10; send_error = errors[i];
        assert(send_pending(&s) && !s.end_reason);
        assert(s.tx_retryable_send_count == i + 1 && s.tx_pending_age_ms_max == 30 + 10 * i);
        assert(s.tx_sent == 16 && s.tx_used == sizeof(original) && s.mic_tx_bytes == 4);
        assert(s.tx_since == UINT32_MAX - 19 && !memcmp(s.tx, original, sizeof(original)));
    }
    send_error = send_limit = 0; send_delay = 7; clock_ms += 10;
    assert(send_pending(&s) && s.tx_sent == 0 && s.tx_used == 0);
    assert(s.tx_pending_age_ms_max == 67 && s.send_ms_max == 7);
    assert(s.mic_tx_bytes == RT_FRAME_BYTES && s.tx_retryable_send_count == 3 && send_calls == 6);
    for (size_t i = 0; i < sizeof(original); ++i) assert(s.tx[i] == 0);
    queue_control(&s, "{\"type\":\"ping\"}");
    assert(send_pending(&s) && s.tx_pending_age_ms_max == 67 && s.mic_tx_bytes == RT_FRAME_BYTES);
    assert(s.tx_no_writable_count == 0 && s.mic_tx_batch_max == 0);
    queue_control(&s, "{\"type\":\"ping\"}");
    s.tx_retryable_send_count = UINT32_MAX - 1; send_error = UNW_EAGAIN;
    assert(send_pending(&s) && send_pending(&s) && s.tx_retryable_send_count == UINT32_MAX);
    send_error = -20;
    assert(!send_pending(&s) && s.tx_retryable_send_count == UINT32_MAX && s.end_errno == -20);
    assert(!strcmp(s.end_reason, "send_error") && s.tx_sent == 0 && s.tx_used != 0);
}
static void test_invalid_send_counts_still_fail_without_progress(void)
{
    for (unsigned i = 0; i < 2; ++i) {
        rt_session_t s = {0}; s.socket = 4;
        queue_control(&s, "{\"type\":\"ping\"}");
        size_t queued = s.tx_used;
        forced_send_result = i ? (int)queued + 1 : 0;
        assert(!send_pending(&s) && !strcmp(s.end_reason, "send_count"));
        assert(s.tx_sent == 0 && s.tx_used == queued && s.mic_tx_bytes == 0);
        assert(s.tx_retryable_send_count == 0 && s.end_errno == 0);
    }
}
static void test_read_only_polls_preserve_pending_batch_and_tail(void)
{
    rt_session_t s = {0}; s.socket = 4; s.ready = true; rt_current = &s;
    rt_ring_init(&s.mic, s.mic_storage, sizeof(s.mic_storage));
    uint8_t pcm[2 * RT_FRAME_BYTES + 17];
    for (size_t i = 0; i < sizeof(pcm); ++i) pcm[i] = (uint8_t)i;
    assert(rt_ring_write(&s.mic, pcm, sizeof(pcm)));
    recv_result = -1; recv_error = UNW_EAGAIN; no_write_polls = 3; stop_after_send = true;
    run_session(&s);
    rt_header_t header;
    assert(rt_header_decode(&header, outbound) && header.length == 2 * RT_FRAME_BYTES);
    assert(header.kind == RT_MIC && outbound_size == RT_HEADER_SIZE + header.length);
    assert(!memcmp(outbound + RT_HEADER_SIZE, pcm, header.length));
    assert(s.tx_pending_age_ms_max == 40 && s.tx_no_writable_count == 3);
    assert(s.tx_retryable_send_count == 0 && s.mic_tx_batch_max == 2 * RT_FRAME_BYTES);
    assert(select_calls == 4 && receive_calls == 4 && send_calls == 1);
    assert(s.tx_sent == 0 && s.tx_used == 0 && s.mic_tx_bytes == 2 * RT_FRAME_BYTES);
    uint8_t tail[17];
    assert(rt_ring_read(&s.mic, tail, sizeof(tail)) == sizeof(tail));
    assert(!memcmp(tail, pcm + 2 * RT_FRAME_BYTES, sizeof(tail)));
    rt_current = NULL;
}
static void test_no_writable_counter_saturates(void)
{
    rt_session_t s = {0}; s.socket = 4; rt_current = &s;
    queue_control(&s, "{\"type\":\"ping\"}");
    s.tx_no_writable_count = UINT32_MAX - 1;
    recv_result = -1; recv_error = UNW_EAGAIN; no_write_polls = 3; stop_after_send = true;
    run_session(&s);
    assert(s.tx_no_writable_count == UINT32_MAX && s.tx_pending_age_ms_max == 40);
    assert(s.mic_tx_batch_max == 0 && s.tx_retryable_send_count == 0 && send_calls == 1);
    rt_current = NULL;
}
static void test_select_retries_are_not_no_writable_or_send_retries(void)
{
    rt_session_t s = {0}; s.socket = 4; rt_current = &s;
    queue_control(&s, "{\"type\":\"ping\"}");
    size_t queued = s.tx_used;
    select_result = -1; select_error = UNW_EINTR; stop_after_polls = 3;
    run_session(&s);
    assert(s.tx_pending_age_ms_max == 30 && s.tx_no_writable_count == 0);
    assert(s.tx_retryable_send_count == 0 && send_calls == 0 && !s.end_reason && !s.failure);
    assert(s.tx_used == queued && s.tx_sent == 0 && s.tx_since == 0);
    rt_current = NULL;
}
static void test_idle_polls_and_incomplete_mic_frame_do_not_count(void)
{
    rt_session_t s = {0}; s.socket = 4; s.ready = true; rt_current = &s;
    rt_ring_init(&s.mic, s.mic_storage, sizeof(s.mic_storage));
    uint8_t pcm[RT_FRAME_BYTES - 1] = {0};
    assert(rt_ring_write(&s.mic, pcm, sizeof(pcm)));
    clock_ms = 100; select_result = 0; stop_after_polls = 3;
    run_session(&s);
    assert(s.tx_pending_age_ms_max == 0 && s.tx_no_writable_count == 0);
    assert(s.tx_retryable_send_count == 0 && s.mic_tx_batch_max == 0 && send_calls == 0);
    assert(s.mic.used == sizeof(pcm) && select_calls == 3);
    rt_current = NULL;
}
int main(void)
{
    reset(); session();
    assert(strstr(summary, "reason=peer_eof errno=0 "));
    assert(strstr(summary, "mic_tx_bytes=0 speaker_rx_bytes=0"));
    assert(strstr(summary, "tx_pending_age_ms=10 tx_no_writable_count=0 tx_retryable_send_count=0 mic_tx_batch_max=0"));
    reset(); connect_result = -1; connect_error = -215; select_result = 0;
    session(); assert(strstr(summary, "reason=connect_timeout errno=0 connect_errno=-215"));
    reset(); select_result = -1; select_error = -3;
    session(); assert(strstr(summary, "reason=select_error errno=-3 "));
    reset(); exception_ready = true;
    session(); assert(strstr(summary, "reason=select_exception errno=0 "));
    reset(); exception_ready = true; select_error = UNW_EAGAIN;
    session(); assert(strstr(summary, "reason=select_exception errno=0 "));
    assert(select_calls == 2); /* main select + cleanup, never retry stale errno */
    reset(); connect_result = -1; connect_error = -215; exception_ready = true;
    session(); assert(strstr(summary, "reason=connect_exception errno=0 connect_errno=-215"));
    reset(); connect_result = -1; connect_error = -18; select_result = 0;
    session(); assert(strstr(summary, "reason=connect_timeout errno=0 connect_errno=-18"));
    reset(); recv_result = -1;
    session(); assert(strstr(summary, "reason=recv_error errno=-20 "));
    reset(); injected_failure = "麦克风队列溢出";
    session(); assert(strstr(summary, "reason=mic_overflow errno=0 "));
    reset(); injected_failure = "private-failure-canary";
    session(); assert(strstr(summary, "reason=session_failure errno=0 "));
    assert(!strstr(summary, "private-failure-canary"));
    reset();
    const char ready[] = "{\"type\":\"ready\"}";
    const char start[] = "{\"type\":\"audio_start\"}";
    const char transcript[] = "{\"type\":\"transcript\",\"role\":\"user\",\"text\":\"private-text-canary\"}";
    uint8_t pcm[RT_FRAME_BYTES] = {0};
    memcpy(pcm, "private-audio-canary", sizeof("private-audio-canary"));
    append_frame(RT_CONTROL, 0, ready, sizeof(ready) - 1);
    append_frame(RT_CONTROL, 1, start, sizeof(start) - 1);
    append_frame(RT_CONTROL, 0, transcript, sizeof(transcript) - 1);
    append_frame(RT_SPEAKER, 1, pcm, sizeof(pcm));
    player_stop_delay = 50;
    session(); assert(strstr(summary, "ready=1 mic_tx_bytes=0 speaker_rx_bytes=640"));
    assert(strstr(summary, "select_ms=10 recv_ms=0 control_ms=50"));
    assert(strstr(summary, "tx_pending_age_ms=60 tx_no_writable_count=0 tx_retryable_send_count=0 mic_tx_batch_max=0"));
    assert(!strstr(summary, "private-text-canary") && !strstr(summary, "private-audio-canary"));
    reset(); select_result = 0; session();
    assert(strstr(summary, "reason=send_timeout errno=0 "));
    assert(strstr(summary, "tx_pending_age_ms=2010 tx_no_writable_count=201 tx_retryable_send_count=0 mic_tx_batch_max=0"));
    assert(send_calls == 0); /* Cleanup STOP must not alter the emitted session counters. */
    reset(); test_payload_counts();
    reset(); test_microphone_catches_up_after_scheduler_delay(RT_FRAME_BYTES);
    reset(); test_microphone_catches_up_after_scheduler_delay(6 * RT_FRAME_BYTES);
    reset(); test_microphone_catches_up_after_scheduler_delay(RT_MIC_CAPACITY);
    reset(); test_pending_age_survives_partial_sends_retries_and_clock_wrap();
    reset(); test_invalid_send_counts_still_fail_without_progress();
    reset(); test_read_only_polls_preserve_pending_batch_and_tail();
    reset(); test_no_writable_counter_saturates();
    reset(); test_select_retries_are_not_no_writable_or_send_retries();
    reset(); test_idle_polls_and_incomplete_mic_frame_do_not_count();
    puts("network diagnostics passed: end reasons, errno, privacy, pending age/wrap, poll/retry saturation, unchanged sends/timeouts, MIC batch bounds/order, speaker frames");
    return 0;
}
