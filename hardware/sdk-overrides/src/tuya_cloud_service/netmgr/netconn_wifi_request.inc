/* Included by netconn_wifi.c and the host adapter: one task mutex owns both
 * the eligibility check and submission. Never hold an IRQ mask over SDK work. */
static MUTEX_HANDLE wifi_request_mutex;
static MUTEX_HANDLE wifi_netcfg_mutex;
static bool wifi_request_owned;

static OPERATE_RET wifi_request_init(void)
{
    if (!wifi_request_mutex) {
        OPERATE_RET result = tal_mutex_create_init(&wifi_request_mutex);
        if (result != OPRT_OK) return result;
    }
    if (!wifi_netcfg_mutex) return tal_mutex_create_init(&wifi_netcfg_mutex);
    return OPRT_OK;
}

/* Stop callbacks may block before the SDK frees its session/list. A separate
 * task mutex covers that complete lifecycle; request ownership remains free
 * to change while a stop is pending. Never nest it under wifi_request_mutex. */
OPERATE_RET netconn_wifi_config_lock(void)
{
    if (!wifi_netcfg_mutex) return OPRT_RESOURCE_NOT_READY;
    return tal_mutex_lock(wifi_netcfg_mutex);
}
void netconn_wifi_config_unlock(void) { tal_mutex_unlock(wifi_netcfg_mutex); }
OPERATE_RET netconn_wifi_stop_config_type(int type)
{
    OPERATE_RET result = netconn_wifi_config_lock();
    if (result != OPRT_OK) return result;
    result = netcfg_stop(type);
    netconn_wifi_config_unlock();
    return result;
}
OPERATE_RET netconn_wifi_stop_config(void)
{
    return netconn_wifi_stop_config_type(NETCFG_STOP_ALL_CFG_MODULE);
}
static OPERATE_RET wifi_request_begin(bool claim)
{
    if (!wifi_request_mutex) return OPRT_RESOURCE_NOT_READY;
    OPERATE_RET result = tal_mutex_lock(wifi_request_mutex);
    if (result == OPRT_OK && claim) wifi_request_owned = true;
    return result;
}
static void wifi_request_end(void) { tal_mutex_unlock(wifi_request_mutex); }

void netconn_wifi_cancel_restore(void)
{
    if (wifi_request_begin(true) == OPRT_OK) wifi_request_end();
}

/* The callback may block, but must not reenter request/get APIs. Used to bind
 * an actual-association check and static-IP submission to the same owner. */
OPERATE_RET netconn_wifi_run_if_current(const char *ssid, OPERATE_RET (*run)(void *), void *arg)
{
    OPERATE_RET result = wifi_request_begin(false);
    if (result != OPRT_OK) return result;
    result = OPRT_RESOURCE_NOT_READY;
    if (ssid && run && !strncmp(s_netmgr_wifi.conn.wifi_conn_info.ssid, ssid,
                               sizeof(s_netmgr_wifi.conn.wifi_conn_info.ssid))) result = run(arg);
    wifi_request_end();
    return result;
}

OPERATE_RET netconn_wifi_restore_if_idle(const netconn_wifi_info_t *info, bool (*allowed)(void))
{
    OPERATE_RET result = wifi_request_begin(false);
    if (result != OPRT_OK) return result;
    WF_STATION_STAT_E station = WSS_IDLE;
    result = OPRT_RESOURCE_NOT_READY;
    if (info && allowed && !wifi_request_owned && !s_netmgr_wifi.conn.wifi_conn_info.ssid[0] &&
        s_netmgr_wifi.base.status == NETMGR_LINK_DOWN &&
        tal_wifi_station_get_status(&station) == OPRT_OK && station == WSS_IDLE && allowed()) {
        wifi_request_owned = true;
        memcpy(&s_netmgr_wifi.conn.wifi_conn_info, info, sizeof(*info));
        result = __netconn_wifi_connect();
    }
    wifi_request_end();
    return result;
}
