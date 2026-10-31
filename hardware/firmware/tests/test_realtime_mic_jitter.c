/* Actual capture callback, ring, network loop and wire parser; virtual time,
 * no sockets. Only the TAL clock/readiness/send boundary is substituted. */
#include "tal_network.h"
#include "netconn_wifi.h"
#include "../app_breathon_realtime_network.c"

static rt_session_t session;
static rt_parser_t receiver;
static uint32_t now_ms, stall_ms, capture_until;
static size_t captured, received;
static unsigned critical_depth, recovered;

static void require(bool ok, const char *message)
{
    if (ok) return;
    fprintf(stderr, "FAIL stall=%ums at %ums: %s; ring=%zu/%zu captured=%zu received=%zu\n",
            stall_ms, now_ms, message, session.mic.used, session.mic.capacity, captured, received);
    exit(1);
}
static uint8_t sample_byte(size_t offset) { return (uint8_t)(offset % 251); }
static void capture_frame(void)
{
    uint8_t pcm[RT_FRAME_BYTES];
    for (size_t i = 0; i < sizeof(pcm); ++i) pcm[i] = sample_byte(captured + i);
    AI_NOTIFY_MIC_DATA_T mic = {pcm, sizeof(pcm)};
    AI_NOTIFY_EVENT_T event = {AI_USER_EVT_MIC_DATA, &mic};
    app_breathon_realtime_event(&event);
    if (!session.mic_overflow) captured += sizeof(pcm);
}
static bool receive_frame(void *arg, const rt_header_t *h, const uint8_t *pcm)
{
    (void)arg;
    if (h->kind == RT_CONTROL) return true; /* Worker heartbeat. */
    require(h->kind == RT_MIC && h->generation == 0, "invalid MIC header");
    require(h->length && h->length % RT_FRAME_BYTES == 0 &&
            h->length <= RT_PAYLOAD_MAX / RT_FRAME_BYTES * RT_FRAME_BYTES, "invalid MIC batch");
    for (size_t i = 0; i < h->length; ++i)
        require(pcm[i] == sample_byte(received + i), "PCM lost, duplicated or reordered");
    received += h->length;
    return true;
}
uint32_t tal_system_enter_critical(void) { return critical_depth++; }
void tal_system_exit_critical(uint32_t saved)
{ require(critical_depth == saved + 1, "critical section mismatch"); --critical_depth; }
uint32_t tal_system_get_millisecond(void) { return now_ms; }
OPERATE_RET netmgr_conn_get(int type, int cmd, netmgr_status_e *status)
{ (void)type; (void)cmd; *status = NETMGR_LINK_UP; return OPRT_OK; }
void app_breathon_realtime_publish(const char *state, const char *text)
{ (void)state; (void)text; }
bool rt_receive_frame(void *arg, const rt_header_t *h, const uint8_t *data)
{ (void)arg; (void)h; (void)data; require(false, "unexpected inbound frame"); return false; }
int tal_net_get_errno(void) { return UNW_EAGAIN; }
int tal_net_recv(int fd, void *data, uint32_t size)
{ (void)fd; (void)data; (void)size; require(false, "unexpected receive"); return -1; }
int tal_net_select(int fd, TUYA_FD_SET_T *r, TUYA_FD_SET_T *w, TUYA_FD_SET_T *e, uint32_t ms)
{
    require(fd == 5 && ms > 0 && !critical_depth, "invalid select");
    /* Observed worker cadence is 20ms, including scheduling around select. */
    now_ms += 20;
    require(now_ms <= capture_until + 500, "backlog failed to recover boundedly");
    if (now_ms <= capture_until) capture_frame();
    TAL_FD_ZERO(r); TAL_FD_ZERO(e);
    if (w) TAL_FD_ZERO(w);
    unsigned phase = now_ms % 500;
    if (stall_ms <= 200 && phase == 400) {
        require(session.mic.used <= RT_FRAME_BYTES &&
                session.tx_used <= RT_HEADER_SIZE + RT_FRAME_BYTES, "backlog grows between stalls");
        ++recovered;
    }
    bool writable = stall_ms > 200 ? now_ms > stall_ms : phase == 0 || phase > stall_ms;
    if (w && writable) { TAL_FD_SET(4, w); return 1; }
    return 0;
}
int tal_net_send(int fd, const void *data, uint32_t size)
{
    require(fd == 4 && !critical_depth, "invalid send");
    /* Whole queued batch accepted after readiness; batching is production code. */
    require(rt_parser_feed(&receiver, data, size, receive_frame, NULL), "invalid outbound wire stream");
    if (now_ms > capture_until && received == captured && !session.mic.used) rt_request = 3;
    return (int)size;
}
static void run_scenario(uint32_t blocked_ms)
{
    memset(&session, 0, sizeof(session)); memset(&receiver, 0, sizeof(receiver));
    now_ms = 0; stall_ms = blocked_ms; capture_until = blocked_ms > 200 ? 1000 : 10000;
    captured = received = 0; critical_depth = recovered = 0;
    session.socket = 4; session.ready = session.capture_on = true;
    rt_current = &session; rt_request = 2;
    rt_ring_init(&session.mic, session.mic_storage, sizeof(session.mic_storage));
    capture_frame(); /* A real frame is pending when the first stall starts. */
    run_session(&session);
    require(!critical_depth, "critical section leaked");
    if (blocked_ms <= 200) {
        require(!session.failure && !session.mic_overflow, "recoverable WiFi jitter closed session");
        require(received == captured && received == 501 * RT_FRAME_BYTES, "PCM not fully delivered");
        require(!session.mic.used && !session.tx_used && !receiver.header_used && !receiver.payload_used,
                "undrained data after capture stopped");
        require(recovered == 20, "not every stall recovered");
        require(session.mic_tx_batch_max == 6 * RT_FRAME_BYTES, "catch-up batching not exercised");
        require(session.tx_pending_age_ms_max >= blocked_ms && session.tx_no_writable_count >= 180,
                "stall not exercised");
        require(session.mic_highwater <= 12 * RT_FRAME_BYTES, "unexpected recovery queue growth");
    } else {
        require(session.mic_overflow && !session.capture_on && session.failure &&
                !strcmp(failure_code(session.failure), "mic_overflow"), "over-budget stall must fail closed");
        require(now_ms == (RT_MIC_CAPACITY / RT_FRAME_BYTES + 1) * 20 && now_ms <= 340,
                "overflow exceeded capture budget");
        require(session.mic.used == RT_MIC_CAPACITY && session.mic_highwater == RT_MIC_CAPACITY,
                "overflow changed queue capacity or discarded PCM");
        require(received == 0 && captured == RT_MIC_CAPACITY + RT_FRAME_BYTES,
                "overflow was not all-or-nothing");
        /* Inspect every accepted byte, including the in-flight frame, after failure. */
        require(rt_parser_feed(&receiver, session.tx, session.tx_used, receive_frame, NULL), "pending frame lost");
        uint8_t pcm[RT_FRAME_BYTES];
        while (session.mic.used) {
            require(rt_ring_read(&session.mic, pcm, sizeof(pcm)) == sizeof(pcm), "ring tail truncated");
            for (size_t i = 0; i < sizeof(pcm); ++i)
                require(pcm[i] == sample_byte(received + i), "overflow overwrote accepted PCM");
            received += sizeof(pcm);
        }
        require(received == captured, "accepted PCM missing at failure");
    }
    printf("PASS MIC stall=%ums: %s, elapsed=%ums highwater=%u/%zu bytes captured=%zu verified=%zu\n",
           blocked_ms, blocked_ms <= 200 ? "20 recoveries, FIFO/no loss" : "hard overflow",
           now_ms, session.mic_highwater, session.mic.capacity, captured, received);
    rt_current = NULL; rt_request = 0;
}
int main(void)
{
    run_scenario(180); run_scenario(200); run_scenario(400);
    return 0;
}
