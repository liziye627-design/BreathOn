/* Persist only an already verified static LAN address, scoped to its SSID. */
#include "tal_api.h"
#include "tal_kv.h"
#include "tal_wifi.h"
#include "netconn_wifi.h"
#include "realtime_wire.h"

struct cli_command {
    const char *name, *help;
    void (*function)(char *, int, int, char **);
};
extern int cli_register_command(const struct cli_command *command);
extern int breathon_set_cp_ipv4(const char *, const char *, const char *, const char *);
extern bool app_breathon_realtime_configured(void);
extern void app_breathon_wifi_restore_boot(const char *scope_ssid);
extern bool breathon_wifi_associated_ssid_matches(const char *ssid);
typedef struct {
    char magic[4], ssid[WIFI_SSID_LEN + 1], address[4][16];
} static_config_t;
static static_config_t saved;
static bool enabled;
static THREAD_HANDLE worker;

static bool valid_config(const static_config_t *c)
{
    if (memcmp(c->magic, "BIP\1", 4) || !c->ssid[0] ||
        !memchr(c->ssid, 0, sizeof(c->ssid))) return false;
    for (unsigned i = 0; i < 4; ++i)
        if (!memchr(c->address[i], 0, 16) || !rt_ipv4_valid(c->address[i])) return false;
    return true;
}
static bool matching_ssid(const char *ssid)
{
    netconn_wifi_info_t info = {0};
    OPERATE_RET result = netmgr_conn_get(NETCONN_WIFI, NETCONN_CMD_SSID_PSWD, &info);
    bool matches = result == OPRT_OK && !strncmp(info.ssid, ssid, sizeof(info.ssid)) &&
                   breathon_wifi_associated_ssid_matches(ssid);
    volatile uint8_t *p = (volatile uint8_t *)&info;
    for (size_t i = 0; i < sizeof(info); ++i) p[i] = 0;
    return matches;
}
static OPERATE_RET apply_static_if_associated(void *arg)
{
    const static_config_t *current = arg;
    WF_STATION_STAT_E state = WSS_IDLE;
    if (!app_breathon_realtime_configured() ||
        tal_wifi_station_get_status(&state) != OPRT_OK || state != WSS_CONN_SUCCESS ||
        !breathon_wifi_associated_ssid_matches(current->ssid)) return OPRT_RESOURCE_NOT_READY;
    int result = breathon_set_cp_ipv4(current->address[0], current->address[1],
                                    current->address[2], current->address[3]);
    PR_NOTICE("BREATHON_STATIC_IP restore request_rc=%d", result);
    return result;
}
static void restore_static_ip(void *arg)
{
    (void)arg;
    for (;;) {
        tal_system_sleep(1000);
        static_config_t current;
        uint32_t irq = tal_system_enter_critical();
        bool active = enabled;
        current = saved;
        tal_system_exit_critical(irq);
        app_breathon_wifi_restore_boot(active ? current.ssid : NULL);
        if (!app_breathon_realtime_configured()) continue;
        if (!active) continue;
        if (netconn_wifi_run_if_current(current.ssid, apply_static_if_associated, &current) == OPRT_OK)
            tal_system_sleep(4000);
    }
}
static OPERATE_RET start_worker(void)
{
    if (worker) return OPRT_OK;
    THREAD_CFG_T cfg = {.thrdname="static_ip", .priority=THREAD_PRIO_2, .stackDepth=4096};
    return tal_thread_create_and_start(&worker, NULL, NULL, restore_static_ip, NULL, &cfg);
}
static void command(char *output, int output_size, int argc, char **argv)
{
    (void)output; (void)output_size;
    if (argc == 2 && !strcmp(argv[1], "clear")) {
        OPERATE_RET result = tal_kv_del("breathon_ip_v1");
        if (result == OPRT_OK) {
            uint32_t irq = tal_system_enter_critical(); enabled = false; tal_system_exit_critical(irq);
        }
        PR_NOTICE("BREATHON_STATIC_IP clear result=%d", result);
        return;
    }
    static_config_t config = {0};
    if (argc != 6 || !argv[1][0] || strlen(argv[1]) > WIFI_SSID_LEN) goto invalid;
    memcpy(config.magic, "BIP\1", 4);
    memcpy(config.ssid, argv[1], strlen(argv[1]));
    for (unsigned i = 0; i < 4; ++i) {
        if (strlen(argv[i+2]) >= 16) goto invalid;
        memcpy(config.address[i], argv[i+2], strlen(argv[i+2]));
    }
    if (!valid_config(&config)) goto invalid;
    NW_IP_S ip = {0};
    WF_STATION_STAT_E state = WSS_IDLE;
    if (tal_wifi_station_get_status(&state) != OPRT_OK || state != WSS_GOT_IP ||
        tal_wifi_get_ip(WF_STATION, &ip) != OPRT_OK || strcmp(ip.ip, config.address[0]) ||
        !matching_ssid(config.ssid)) {
        PR_NOTICE("BREATHON_STATIC_IP verify connected address and SSID first");
        return;
    }
    OPERATE_RET result = start_worker();
    if (result == OPRT_OK)
        result = tal_kv_set("breathon_ip_v1", (const uint8_t *)&config, sizeof(config));
    if (result == OPRT_OK) {
        uint32_t irq = tal_system_enter_critical();
        saved = config; enabled = true;
        tal_system_exit_critical(irq);
    }
    PR_NOTICE("BREATHON_STATIC_IP persist result=%d", result);
    return;
invalid:
    PR_NOTICE("breathon_wifi_static <ssid> <ip> <mask> <gateway> <dns> | clear");
}
OPERATE_RET app_breathon_wifi_static_init(void)
{
    static const struct cli_command cli = {
        "breathon_wifi_static", "Save verified static LAN address", command
    };
    if (cli_register_command(&cli) != 0) return OPRT_COM_ERROR;
    uint8_t *data = NULL; size_t length = 0;
    if (tal_kv_get("breathon_ip_v1", &data, &length) == OPRT_OK) {
        if (data && length == sizeof(saved)) {
            memcpy(&saved, data, sizeof(saved));
            enabled = valid_config(&saved);
        }
        tal_kv_free(data);
    }
    /* Also services explicit saved Wi-Fi on DHCP, with no static-IP record. */
    return start_worker();
}
