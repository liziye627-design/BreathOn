/* Local maintenance command: persist credentials after a successful Wi-Fi test. */
#include "tal_api.h"
#include "tal_kv.h"
#include "netconn_wifi.h"
#include "netcfg.h"
#include "tal_wifi.h"
#include "app_breathon_wifi_json.h"
#include "app_breathon_realtime_mode.h"
#include "tal_workq_service.h"
/* Minimal Beken CLI ABI from T5AI 69793c66, bk_private/bk_cli.h.
 * Its full header requires generated platform headers unavailable during the
 * earlier TuyaOpen application-library build. Keep these three fields in sync. */
struct cli_command {
    const char *name;
    const char *help;
    void (*function)(char *, int, int, char **);
};
extern int cli_register_command(const struct cli_command *command);
extern OPERATE_RET app_breathon_wifi_static_init(void);
extern bool app_breathon_realtime_configured(void);
extern bool breathon_wifi_associated_ssid_matches(const char *ssid);
static bool boot_pending, boot_scheduled, boot_cancelled;
#define BOOT_SCOPE_KEY "breathon_wifi_v1"

static void wipe(void *data, size_t size)
{
    wifi_secret_wipe(data, size);
}

static bool boot_restore_enabled(void)
{
    return BREATHON_PRESENTATION_ONLY || app_breathon_realtime_configured();
}

/* KV length excludes its appended terminator. Parse within that bound instead
 * of relying on the SDK's permissive/truncating private credential loader. */
static bool load_saved_wifi(netconn_wifi_info_t *info)
{
    uint8_t *data = NULL; size_t length = 0;
    if (tal_kv_get("netinfo", &data, &length) != OPRT_OK) return false;
    bool valid = wifi_json_parse(data, length, info);
    if (data) { wipe(data, length); tal_kv_free(data); }
    return valid;
}

/* Called with the SDK request mutex held; never calls a blocking SDK API. */
static bool boot_restore_allowed(void)
{
    uint32_t irq = tal_system_enter_critical();
    bool allowed = !boot_cancelled;
    tal_system_exit_critical(irq);
    return allowed && boot_restore_enabled();
}

/* Explicit wifi_save scope takes precedence over a legacy static record.
 * Only an absent key permits legacy fallback; malformed/error records fail
 * closed. The new record contains SSID bytes only, without a terminator. */
static bool saved_scope_matches(const char *ssid, const char *legacy_scope)
{
    uint8_t *data = NULL; size_t length = 0;
    OPERATE_RET result = tal_kv_get(BOOT_SCOPE_KEY, &data, &length);
    if (result == OPRT_NOT_FOUND) return legacy_scope && !strcmp(ssid, legacy_scope);
    bool matches = result == OPRT_OK && data && length && length <= WIFI_SSID_LEN &&
                   !memchr(data, 0, length) && strlen(ssid) == length && !memcmp(data, ssid, length);
    if (data) { wipe(data, length); tal_kv_free(data); }
    return matches;
}

/* Called by the existing Wi-Fi/static-IP worker, not the high-priority queue.
 * scope_ssid may be NULL when no valid legacy static record exists. */
void app_breathon_wifi_restore_boot(const char *scope_ssid)
{
    uint32_t irq = tal_system_enter_critical();
    bool pending = boot_pending && !boot_cancelled;
    boot_pending = false;
    tal_system_exit_critical(irq);
    if (!pending || !boot_restore_enabled()) return;
    netconn_wifi_info_t info = {0}, current = {0};
    netmgr_status_e link = NETMGR_LINK_DOWN;
    WF_STATION_STAT_E station = WSS_IDLE;
    if (!load_saved_wifi(&info) || !saved_scope_matches(info.ssid, scope_ssid)) {
        PR_NOTICE("BREATHON_WIFI boot restore skipped: invalid or unscoped netinfo");
        goto done;
    }
    /* Any already requested SSID belongs to SDK activation or explicit user
     * action. Even the same SSID may have a connect queued but not run yet. */
    if (netmgr_conn_get(NETCONN_WIFI, NETCONN_CMD_SSID_PSWD, &current) != OPRT_OK ||
        current.ssid[0] ||
        netmgr_conn_get(NETCONN_WIFI, NETCONN_CMD_STATUS, &link) != OPRT_OK || link != NETMGR_LINK_DOWN ||
        tal_wifi_station_get_status(&station) != OPRT_OK || station != WSS_IDLE) goto done;
    irq = tal_system_enter_critical();
    bool cancelled = boot_cancelled;
    tal_system_exit_critical(irq);
    if (cancelled || !boot_restore_enabled()) goto done;
    OPERATE_RET result = netconn_wifi_stop_config();
    /* Stopping provisioning may block. Do not overwrite a new explicit/SDK
     * connection requested while it was stopping. */
    irq = tal_system_enter_critical();
    cancelled = boot_cancelled;
    tal_system_exit_critical(irq);
    if (cancelled || !boot_restore_enabled() ||
        netmgr_conn_get(NETCONN_WIFI, NETCONN_CMD_SSID_PSWD, &current) != OPRT_OK ||
        current.ssid[0]) goto done;
    if (result == OPRT_OK || result == OPRT_COM_ERROR)
        result = netconn_wifi_restore_if_idle(&info, boot_restore_allowed);
    PR_NOTICE("BREATHON_WIFI boot restore request_rc=%d", result);
done:
    wipe(&info, sizeof(info)); wipe(&current, sizeof(current));
}

static void boot_ready(void *arg)
{
    (void)arg;
    uint32_t irq = tal_system_enter_critical();
    boot_pending = !boot_cancelled;
    tal_system_exit_critical(irq);
}

static int boot_after_netcfg(tuya_iot_config_t *config)
{
    (void)config;
    if (!boot_restore_enabled() || boot_scheduled) return OPRT_OK;
    /* tuya_main registers the SDK Wi-Fi token callback before this one. That
     * callback queues AP/BLE starts on this FIFO. Only mark readiness here;
     * stopping provisioning can block and belongs on the static-IP worker. */
    OPERATE_RET result = tal_workq_schedule(WORKQ_HIGHTPRI, boot_ready, NULL);
    boot_scheduled = result == OPRT_OK;
    if (result != OPRT_OK) PR_ERR("BREATHON_WIFI boot queue result=%d", result);
    return result;
}

static void cancel_boot_restore(void)
{
    /* Ownership cancellation uses the same task mutex as SDK submission. */
    netconn_wifi_cancel_restore();
    uint32_t irq = tal_system_enter_critical();
    boot_cancelled = true;
    tal_system_exit_critical(irq);
}

/* Start through the application network manager. The CP-only `sta` command
 * bypasses its station mode/events and cannot establish the application's link. */
static void connect_wifi(char *output, int output_size, int argc, char **argv)
{
    (void)output;
    (void)output_size;
    if (argc != 3 || strlen(argv[1]) == 0 || strlen(argv[1]) > WIFI_SSID_LEN ||
        strlen(argv[2]) > WIFI_PASSWD_LEN) {
        PR_ERR("BREATHON_WIFI: invalid connection arguments");
        return;
    }
    netconn_wifi_info_t info = {0};
    cancel_boot_restore();
    memcpy(info.ssid, argv[1], strlen(argv[1]));
    memcpy(info.pswd, argv[2], strlen(argv[2]));
    /* Match the SDK's successful provisioning handoff before station mode. */
    OPERATE_RET result = netconn_wifi_stop_config();
    /* Pinned SDK netcfg_stop(ALL) returns COM_ERROR only when its session
     * is already destroyed. A repeated connection request must still work. */
    if (result == OPRT_OK || result == OPRT_COM_ERROR)
        result = netmgr_conn_set(NETCONN_WIFI, NETCONN_CMD_SSID_PSWD, &info);
    wipe(&info, sizeof(info));
    PR_NOTICE("BREATHON_WIFI: connection requested result=%d", result);
}

static void wifi_status(char *output, int output_size, int argc, char **argv)
{
    (void)output; (void)output_size; (void)argc; (void)argv;
    WF_STATION_STAT_E station = WSS_IDLE;
    netmgr_status_e link = NETMGR_LINK_DOWN;
    NW_IP_S ip = {0};
    OPERATE_RET station_result = tal_wifi_station_get_status(&station);
    OPERATE_RET link_result = netmgr_conn_get(NETCONN_WIFI, NETCONN_CMD_STATUS, &link);
    OPERATE_RET ip_result = tal_wifi_get_ip(WF_STATION, &ip);
    PR_NOTICE("BREATHON_WIFI station=%d link=%d status_rc=%d link_rc=%d ip_rc=%d ip=%s",
              station, link, station_result, link_result, ip_result, ip.ip);
}

static void save_wifi(char *output, int output_size, int argc, char **argv)
{
    (void)output;
    (void)output_size;
    if (argc != 3 || strlen(argv[1]) == 0 || strlen(argv[1]) > WIFI_SSID_LEN ||
        strlen(argv[2]) > WIFI_PASSWD_LEN) {
        PR_ERR("Usage: breathon_wifi_save <ssid> <password>");
        return;
    }
    netmgr_status_e status = NETMGR_LINK_DOWN;
    WF_STATION_STAT_E station = WSS_IDLE;
    netconn_wifi_info_t info = {0}, current = {0};
    char value[WIFI_JSON_CAPACITY] = {0};
    size_t length = 0;
    if (netmgr_conn_get(NETCONN_WIFI, NETCONN_CMD_STATUS, &status) != OPRT_OK ||
        status != NETMGR_LINK_UP || tal_wifi_station_get_status(&station) != OPRT_OK ||
        station != WSS_GOT_IP ||
        netmgr_conn_get(NETCONN_WIFI, NETCONN_CMD_SSID_PSWD, &current) != OPRT_OK ||
        strncmp(current.ssid, argv[1], sizeof(current.ssid)) ||
        !breathon_wifi_associated_ssid_matches(argv[1])) {
        PR_ERR("BREATHON_WIFI: connect and verify Wi-Fi before saving");
        goto done;
    }
    cancel_boot_restore();
    memcpy(info.ssid, argv[1], strlen(argv[1]));
    memcpy(info.pswd, argv[2], strlen(argv[2]));
    if (!wifi_json_encode(&info, value, sizeof(value), &length)) goto done;
    OPERATE_RET rt = tal_kv_set("netinfo", (uint8_t *)value, length);
    if (rt == OPRT_OK)
        rt = tal_kv_set(BOOT_SCOPE_KEY, (const uint8_t *)info.ssid, strlen(info.ssid));
    /* This is already the verified association. Saving must not resubmit a
     * stale requested configuration over a newer explicit connection. */
    PR_NOTICE("BREATHON_WIFI: persist result=%d", rt);
done:
    wipe(&info, sizeof(info)); wipe(&current, sizeof(current));
    wipe(value, sizeof(value));
}

OPERATE_RET app_breathon_wifi_register(void)
{
    static const struct cli_command connect_command = {
        "breathon_wifi_connect", "Connect through application network manager", connect_wifi
    };
    static const struct cli_command status_command = {
        "breathon_wifi_status", "Read application Wi-Fi state", wifi_status
    };
    static const struct cli_command command = {
        "breathon_wifi_save", "Save verified Wi-Fi credentials", save_wifi
    };
    if (cli_register_command(&connect_command) != 0) return OPRT_COM_ERROR;
    if (cli_register_command(&status_command) != 0) return OPRT_COM_ERROR;
    if (cli_register_command(&command) != 0) return OPRT_COM_ERROR;
    OPERATE_RET result = app_breathon_wifi_static_init();
    if (result != OPRT_OK) return result;
    return tuya_iot_token_get_port_register(tuya_iot_client_get(), boot_after_netcfg);
}
