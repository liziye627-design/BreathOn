#include "app_breathon_realtime_internal.h"
#include "app_breathon_audio.h"
#include "netconn_wifi.h"
#include "tal_kv.h"
#include "tkl_kws.h"
#include "realtime_request.h"

rt_session_t *rt_current;
bool rt_playback_idle = true;
int rt_request;
static rt_config_t config;
static bool initialized, report_pending, config_pending;
static bool provider_configured;
static bool microphone_muted;
static bool restart_requested;
static THREAD_HANDLE network_task, playback_task;

/* T5 spinlock.c:284-287 reserves exclusive accesses for its SRAM lock section.
 * Keep application state on ordinary load/store; TAL owns the SMP lock. */
static bool request_transition(int expected, int next)
{
    uint32_t irq = tal_system_enter_critical();
    bool matched = __atomic_load_n(&rt_request, __ATOMIC_RELAXED) == expected;
    if (matched) __atomic_store_n(&rt_request, next, __ATOMIC_RELEASE);
    tal_system_exit_critical(irq);
    return matched;
}
static void request_clear(void)
{
    uint32_t irq = tal_system_enter_critical();
    int next=rt_request_finish_next(__atomic_load_n(&rt_request,__ATOMIC_RELAXED),&restart_requested);
    __atomic_store_n(&rt_request, next, __ATOMIC_RELEASE);
    tal_system_exit_critical(irq);
}
bool rt_stop_requested(void) { return __atomic_load_n(&rt_request, __ATOMIC_ACQUIRE) == 3; }
bool app_breathon_realtime_active(void) { return __atomic_load_n(&rt_request, __ATOMIC_ACQUIRE) > 0; }
bool app_breathon_realtime_configured(void) { return __atomic_load_n(&provider_configured, __ATOMIC_ACQUIRE); }
bool rt_wifi_up(void)
{
    netmgr_status_e status = NETMGR_LINK_DOWN;
    return netmgr_conn_get(NETCONN_WIFI, NETCONN_CMD_STATUS, &status) == OPRT_OK && status == NETMGR_LINK_UP;
}
void app_breathon_realtime_start(void)
{
    if (!__atomic_load_n(&initialized, __ATOMIC_ACQUIRE)) return;
    uint32_t irq = tal_system_enter_critical();
    int next=rt_request_start_next(__atomic_load_n(&rt_request,__ATOMIC_RELAXED),&restart_requested,&microphone_muted);
    __atomic_store_n(&rt_request,next,__ATOMIC_RELEASE);
    tal_system_exit_critical(irq);
}
bool app_breathon_realtime_mute(bool muted)
{
    uint32_t irq = tal_system_enter_critical();
    microphone_muted = muted;
    if (muted && rt_current) {
        rt_ring_clear(&rt_current->mic);
    }
    tal_system_exit_critical(irq);
    return true;
}
void app_breathon_realtime_stop(void)
{
    uint32_t irq = tal_system_enter_critical();
    int next=rt_request_stop_next(__atomic_load_n(&rt_request,__ATOMIC_RELAXED),&restart_requested);
    __atomic_store_n(&rt_request,next,__ATOMIC_RELEASE);
    tal_system_exit_critical(irq);
}
OPERATE_RET app_breathon_realtime_config(const char *host, uint16_t port, const char *token)
{
    if (!rt_ipv4_valid(host) || !port || !token) return OPRT_INVALID_PARM;
    size_t length = strlen(token);
    if (!length || length > RT_TOKEN_MAX || strlen(host) >= sizeof(config.host)) return OPRT_INVALID_PARM;
    /* LAN tokens are printable ASCII, JSON escaping is handled by cJSON. */
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)token[i] < 33 || (unsigned char)token[i] > 126) return OPRT_INVALID_PARM;
    if (!request_transition(0, -1)) return OPRT_COM_ERROR;
    /* Versioned fixed wire representation, independent of structure padding. */
    uint8_t record[151] = {'B','R','C',1};
    record[4] = (uint8_t)(port >> 8); record[5] = (uint8_t)port;
    memcpy(record + 6, host, strlen(host)); memcpy(record + 22, token, length);
    OPERATE_RET result = tal_kv_set("breathon_rt_v1", record, sizeof(record));
    memset(record, 0, sizeof(record));
    if (result != OPRT_OK) { request_clear(); return result; }
    uint32_t irq = tal_system_enter_critical();
    memset(&config, 0, sizeof(config));
    memcpy(config.host, host, strlen(host)); memcpy(config.token, token, length); config.port = port;
    config_pending = true;
    tal_system_exit_critical(irq);
    __atomic_store_n(&provider_configured, true, __ATOMIC_RELEASE);
    tkl_kws_disable();
    request_clear();
    return OPRT_OK;
}
static void load_config(void)
{
    uint8_t *record = NULL; size_t n = 0;
    if (tal_kv_get("breathon_rt_v1", &record, &n) != OPRT_OK) return;
    if (n == 151 && !memcmp(record, "BRC\1", 4) && record[21] == 0 && record[150] == 0 &&
        rt_ipv4_valid((char *)record + 6) && (record[4] || record[5]) && record[22]) {
        size_t length = strlen((char *)record + 22); bool valid = true;
        for (size_t i = 0; i < length; ++i) if (record[22+i] < 33 || record[22+i] > 126) valid = false;
        if (valid) {
            memcpy(config.host, record + 6, sizeof(config.host));
            memcpy(config.token, record + 22, sizeof(config.token));
            config.port = (uint16_t)(((uint16_t)record[4] << 8) | record[5]);
            __atomic_store_n(&provider_configured, true, __ATOMIC_RELEASE);
            config_pending = true;
            tkl_kws_disable();
        }
    }
    memset(record, 0, n); tal_kv_free(record);
}
void app_breathon_realtime_status(void)
{
    uint32_t irq = tal_system_enter_critical(); report_pending = true; tal_system_exit_critical(irq);
}
void app_breathon_realtime_event(AI_NOTIFY_EVENT_T *event)
{
    if (!event || event->type != AI_USER_EVT_MIC_DATA || !event->data) return;
    AI_NOTIFY_MIC_DATA_T *mic = event->data;
    uint32_t irq = tal_system_enter_critical();
    rt_session_t *s = rt_current;
    if (s && s->capture_on) {
        /* Preserve the audio clock and let server VAD finish a turn while
         * muted, without forwarding any newly captured microphone samples. */
        static const uint8_t silence[RT_PAYLOAD_MAX] = {0};
        const uint8_t *samples = microphone_muted ? silence : (const uint8_t *)mic->data;
        if (mic->data_len > s->mic_frame_max) s->mic_frame_max = mic->data_len;
        if (!mic->data || !mic->data_len || mic->data_len > RT_PAYLOAD_MAX || (mic->data_len & 1) ||
            !rt_ring_write(&s->mic, samples, mic->data_len)) {
            s->mic_overflow = true; s->capture_on = false;
        }
        if (s->mic.used > s->mic_highwater) s->mic_highwater = (uint32_t)s->mic.used;
    }
    tal_system_exit_critical(irq);
}
void rt_publish_pending(void)
{
    uint32_t irq = tal_system_enter_critical();
        bool report = report_pending, configured = config_pending;
        report_pending = config_pending = false;
        tal_system_exit_critical(irq);
        if (configured) app_breathon_realtime_publish("configured", "网关配置已保存");
        if (report) {
            PR_NOTICE("BREATHON_REALTIME active=%d configured=%d storage=KV", app_breathon_realtime_active(), app_breathon_realtime_configured());
        }
}
static void network_worker(void *arg)
{
    (void)arg;
    for (;;) {
        rt_publish_pending();
        if (request_transition(1, 2)) {
            if (!config.port) app_breathon_realtime_publish("error", "请先配置 Realtime 网关");
            else if (!rt_wifi_up()) app_breathon_realtime_publish("error", "网络未连接");
            else if (!rt_stop_requested()) rt_network_session(&config);
            request_clear();
        } else if (rt_stop_requested()) {
            app_breathon_realtime_publish("stopped", "已停止");
            request_clear();
        }
        tal_system_sleep(10);
    }
}
OPERATE_RET app_breathon_realtime_init(void)
{
    if (initialized) return OPRT_OK;
    load_config();
    THREAD_CFG_T play_cfg = {.thrdname="rt_play", .priority=THREAD_PRIO_2, .stackDepth=4096};
    THREAD_CFG_T net_cfg = {.thrdname="rt_net", .priority=THREAD_PRIO_2, .stackDepth=6144};
    OPERATE_RET rc = tal_thread_create_and_start(&playback_task, NULL, NULL, rt_playback_task, NULL, &play_cfg);
    if (rc != OPRT_OK) return rc;
    rc = tal_thread_create_and_start(&network_task, NULL, NULL, network_worker, NULL, &net_cfg);
    if (rc != OPRT_OK) { tal_thread_delete(playback_task); return rc; }
    rc = rt_cli_register();
    if (rc != OPRT_OK) { tal_thread_delete(network_task); tal_thread_delete(playback_task); return rc; }
    __atomic_store_n(&initialized, true, __ATOMIC_RELEASE);
    return OPRT_OK;
}
