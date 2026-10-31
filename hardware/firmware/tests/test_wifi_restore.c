/* Runs the real application Wi-Fi files; adapters model the pinned SDK's
 * activation/token callback and FIFO workqueue ordering, never real networking. */
#include "tal_api.h"
#include "tal_kv.h"
#include "netconn_wifi.h"
#include "tal_workq_service.h"
#include "netcfg.h"
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include <process.h>
#include "cJSON.h"
#include "app_breathon_wifi_json.h"
#include <windows.h>
/* Do not open Windows' interactive assertion/crash dialog in a host runner. */
#undef assert
#define assert(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); \
} } while (0)

extern int app_breathon_wifi_register(void);
extern void app_breathon_wifi_restore_boot(const char *scope_ssid);
struct cli_command { const char *name, *help; void (*function)(char *, int, int, char **); };
static const struct cli_command *commands[8];
static unsigned command_count;
static void (*worker_fn)(void *);
static tuya_token_get_cb_t boot_callback;
static tuya_iot_client_t client;
static bool provider = true;
static struct {
    struct { netmgr_status_e status; } base;
    struct { netconn_wifi_info_t wifi_conn_info; } conn;
} s_netmgr_wifi;
#define link s_netmgr_wifi.base.status
#define connected s_netmgr_wifi.conn.wifi_conn_info
static WF_STATION_STAT_E station;
static netconn_wifi_info_t requested;
static int connections, stops, static_requests, paid_starts;
static int started_modules, sleep_count;
static jmp_buf tick_done;
static struct { WORKQUEUE_CB cb; void *arg; } queue[8];
static unsigned queue_count;
static const char *credential_json = "{\"s\":\"BreathOn-Lab\",\"p\":\"fake-test-password\"}";
static size_t credential_length;
static uint8_t *credential_allocation;
static size_t credential_allocation_size;
static bool credential_wiped;
static bool expect_restore = true;
static bool reconfigure_during_stop;
static bool static_present = true, saving;
static const char *explicit_scope;
static size_t explicit_scope_length;
static int scope_read_result = OPRT_OK, ssid_read_result = OPRT_OK;
static int netinfo_write_result = OPRT_OK, scope_write_result = OPRT_OK;
static int netinfo_writes, scope_writes;
static char written_netinfo[641], written_scope[WIFI_SSID_LEN + 1];
static const char *expected_ssid = "BreathOn-Lab";
static char associated_ssid[WIFI_SSID_LEN + 1];
static bool association_error, handoff_race, check_json_allocations;
static unsigned json_allocations;
static _Thread_local unsigned irq_depth, request_lock_depth, netcfg_lock_depth;
static _Thread_local bool lifecycle_secondary;
static bool lifecycle_test, lifecycle_bypass;
static HANDLE stop_first_blocked, stop_first_release, stop_second_attempt;
static CRITICAL_SECTION lifecycle_lock;
/* Retain a tombstone instead of dereferencing actual freed memory. The model
 * marks logical destruction exactly where SDK netcfg_uninit frees the list. */
static struct {
    bool live, started[2];
    unsigned calls, callbacks, frees, after_free;
} cfg_session;
static int lifecycle_first_type, lifecycle_second_result;
static bool lifecycle_explicit;
static int __netconn_wifi_connect(void);
#include "../../vendor/TuyaOpen-official/src/tuya_cloud_service/netmgr/netconn_wifi_request.inc"
static void *count_json_alloc(size_t size) { ++json_allocations; return malloc(size); }
bool breathon_wifi_associated_ssid_matches(const char *ssid)
{
    return !association_error && memchr(associated_ssid, 0, sizeof(associated_ssid)) &&
           !strcmp(associated_ssid, ssid);
}
static struct { char magic[4], ssid[33], address[4][16]; } scope = {
    {'B','I','P',1}, "BreathOn-Lab", {"192.168.137.250", "255.255.255.0", "192.168.137.2", "192.168.137.2"}
};
static char log_buffer[4096];

void wifi_test_log(const char *format, ...)
{
    size_t n = strlen(log_buffer);
    va_list args; va_start(args, format);
    vsnprintf(log_buffer+n, sizeof(log_buffer)-n, format, args);
    va_end(args);
}
uint32_t tal_system_enter_critical(void) { return irq_depth++; }
void tal_system_exit_critical(uint32_t irq) { assert(irq_depth == irq + 1); --irq_depth; }
int tal_mutex_create_init(MUTEX_HANDLE *handle)
{
    CRITICAL_SECTION *mutex = malloc(sizeof(*mutex)); assert(mutex);
    InitializeCriticalSection(mutex); *handle = mutex; return OPRT_OK;
}
int tal_mutex_lock(MUTEX_HANDLE handle)
{
    assert(!irq_depth && !request_lock_depth && !netcfg_lock_depth);
    if (handle == wifi_request_mutex) {
        EnterCriticalSection(handle); ++request_lock_depth;
    } else {
        assert(handle == wifi_netcfg_mutex);
        if (lifecycle_test && lifecycle_secondary) {
            /* Prove contention, rather than infer it from a short sleep. */
            assert(!TryEnterCriticalSection(handle));
            SetEvent(stop_second_attempt);
        }
        EnterCriticalSection(handle); ++netcfg_lock_depth;
    }
    return OPRT_OK;
}
int tal_mutex_unlock(MUTEX_HANDLE handle)
{
    assert(!irq_depth);
    if (handle == wifi_request_mutex) {
        assert(request_lock_depth == 1 && !netcfg_lock_depth); --request_lock_depth;
    } else {
        assert(handle == wifi_netcfg_mutex && netcfg_lock_depth == 1 && !request_lock_depth);
        --netcfg_lock_depth;
    }
    LeaveCriticalSection(handle); return OPRT_OK;
}
void tal_system_sleep(uint32_t ms)
{
    (void)ms;
    if (++sleep_count > 1) longjmp(tick_done, 1);
}
int tal_thread_create_and_start(THREAD_HANDLE *h, void (*a)(void), void (*b)(void), void (*fn)(void *), const void *arg, const THREAD_CFG_T *cfg)
{
    (void)a; (void)b; (void)arg; (void)cfg;
    worker_fn = fn; *h = (void *)1; return OPRT_OK;
}
int cli_register_command(const struct cli_command *command)
{
    assert(command_count < 8); commands[command_count++] = command; return 0;
}
int tal_kv_get(const char *key, uint8_t **data, size_t *n)
{
    if (!strcmp(key, "breathon_ip_v1")) { /* gitleaks:allow -- storage key name, not a credential */
        if (!static_present) return OPRT_NOT_FOUND;
        *n = sizeof(scope); *data = malloc(*n); memcpy(*data, &scope, *n); return 0;
    }
    if (!strcmp(key, "breathon_wifi_v1")) {
        if (scope_read_result != OPRT_OK) return scope_read_result;
        if (!explicit_scope) return OPRT_NOT_FOUND;
        *n = explicit_scope_length ? explicit_scope_length : strlen(explicit_scope);
        *data = malloc(*n ? *n : 1); memcpy(*data, explicit_scope, *n); return 0;
    }
    assert(!strcmp(key, "netinfo"));
    if (!credential_json) return OPRT_NOT_FOUND;
    *n = credential_length ? credential_length : strlen(credential_json);
    *data = malloc(*n); memcpy(*data, credential_json, *n);
    credential_allocation = *data; credential_allocation_size = *n; return 0;
}
int tal_kv_set(const char *key, const uint8_t *data, size_t n)
{
    if (!strcmp(key, "netinfo")) {
        ++netinfo_writes;
        if (netinfo_write_result != OPRT_OK) return netinfo_write_result;
        assert(n < sizeof(written_netinfo));
        memcpy(written_netinfo, data, n); written_netinfo[n] = 0;
        credential_json = written_netinfo; credential_length = n; return 0;
    }
    assert(!strcmp(key, "breathon_wifi_v1"));
    ++scope_writes;
    if (scope_write_result != OPRT_OK) return scope_write_result;
    assert(n > 0 && n <= WIFI_SSID_LEN && !memchr(data, 0, n));
    memcpy(written_scope, data, n); written_scope[n] = 0;
    explicit_scope = written_scope; explicit_scope_length = n; return 0;
}
int tal_kv_del(const char *key) { (void)key; return 0; }
int tal_kv_free(uint8_t *p)
{
    if (p == credential_allocation) {
        credential_wiped = true;
        for (size_t i=0; i<credential_allocation_size; ++i) if (p[i]) credential_wiped=false;
        credential_allocation = NULL;
    }
    free(p); return 0;
}
bool app_breathon_realtime_configured(void) { return provider; }
void app_breathon_realtime_start(void) { ++paid_starts; }
tuya_iot_client_t *tuya_iot_client_get(void) { return &client; }
int tuya_iot_token_get_port_register(tuya_iot_client_t *c, tuya_token_get_cb_t fn)
{ assert(c == &client); boot_callback = fn; return 0; }
int tal_workq_schedule(WORKQ_SERVICE_E service, WORKQUEUE_CB cb, void *arg)
{
    assert(service == WORKQ_HIGHTPRI); assert(queue_count < 8);
    queue[queue_count].cb=cb; queue[queue_count++].arg=arg; return 0;
}
int netmgr_conn_get(int type, int cmd, void *data)
{
    assert(type == NETCONN_WIFI);
    if (cmd == NETCONN_CMD_STATUS) *(netmgr_status_e *)data = link;
    else {
        assert(cmd == NETCONN_CMD_SSID_PSWD);
        *(netconn_wifi_info_t *)data = connected;
        if (handoff_race && stops) {
            handoff_race = false;
            netconn_wifi_info_t newer = {0}; strcpy(newer.ssid, "Newer-explicit");
            assert(netmgr_conn_set(NETCONN_WIFI, NETCONN_CMD_SSID_PSWD, &newer) == OPRT_OK);
        }
        return ssid_read_result;
    }
    return 0;
}
int netmgr_conn_set(int type, int cmd, void *data)
{
    assert(type == NETCONN_WIFI && cmd == NETCONN_CMD_SSID_PSWD);
    assert(wifi_request_begin(true) == OPRT_OK);
    connected = *(netconn_wifi_info_t *)data;
    int result = __netconn_wifi_connect();
    wifi_request_end(); return result;
}
static int __netconn_wifi_connect(void)
{
    assert(!irq_depth && request_lock_depth == 1);
    if (!saving) { assert(started_modules == 2); assert(stops == (lifecycle_test ? 2 : 1)); }
    requested = connected; ++connections;
    station=WSS_CONNECTING; return 0;
}
int netcfg_stop(int mode)
{
    assert(!irq_depth && !request_lock_depth);
    assert(started_modules == 2);
    if (lifecycle_test) {
        assert(mode == 0 || mode == 1 || mode == 2);
        EnterCriticalSection(&lifecycle_lock);
        ++cfg_session.calls; ++stops;
        if (!cfg_session.live) {
            LeaveCriticalSection(&lifecycle_lock);
            return mode ? OPRT_INVALID_PARM : OPRT_COM_ERROR;
        }
        for (unsigned i = 0; i < 2; ++i) {
            if (mode && mode != (1 << i)) continue;
            if (!cfg_session.live) { ++cfg_session.after_free; break; }
            if (cfg_session.started[i]) {
                cfg_session.started[i] = false;
                ++cfg_session.callbacks;
                if (cfg_session.callbacks == 1) {
                    SetEvent(stop_first_blocked);
                    LeaveCriticalSection(&lifecycle_lock);
                    assert(WaitForSingleObject(stop_first_release, 5000) == WAIT_OBJECT_0);
                    EnterCriticalSection(&lifecycle_lock);
                    /* The SDK next follows the saved list cursor here. */
                    if (!cfg_session.live) { ++cfg_session.after_free; break; }
                }
            }
        }
        if (!mode) { cfg_session.live = false; ++cfg_session.frees; }
        LeaveCriticalSection(&lifecycle_lock);
        return OPRT_OK;
    }
    assert(mode == 0 && netcfg_lock_depth == 1); ++stops;
    if (reconfigure_during_stop) strcpy(connected.ssid, "Other");
    return 0;
}
int tal_wifi_station_get_status(WF_STATION_STAT_E *s) { *s=station; return 0; }
int tal_wifi_get_ip(int type, NW_IP_S *ip)
{ (void)type; strcpy(ip->ip, scope.address[0]); return 0; }
int breathon_set_cp_ipv4(const char *a, const char *b, const char *c, const char *d)
{
    assert(!irq_depth && request_lock_depth == 1);
    assert(!strcmp(a, scope.address[0]) && !strcmp(b, scope.address[1]));
    assert(!strcmp(c, scope.address[2]) && !strcmp(d, scope.address[3]));
    ++static_requests; station=WSS_GOT_IP; link=NETMGR_LINK_UP; return 0;
}
static void tick(void)
{
    sleep_count=0;
    if (worker_fn && !setjmp(tick_done)) worker_fn(NULL);
}
static void start_module(void *arg) { (void)arg; ++started_modules; }
static void token_pending(void)
{
    /* netconn_wifi_open on unbound SDK does NOT load netinfo. Its first token
     * callback queues AP/BLE starts; app callback was registered afterwards. */
    tal_workq_schedule(WORKQ_HIGHTPRI, start_module, NULL);
    tal_workq_schedule(WORKQ_HIGHTPRI, start_module, NULL);
    if (boot_callback) { tuya_iot_config_t c={0}; assert(boot_callback(&c)==0); }
}
static void drain_queue(void)
{
    for (unsigned i=0; i<queue_count; ++i) queue[i].cb(queue[i].arg);
    queue_count=0;
}
static void select_case(const char *name)
{
    expect_restore = false;
    if (!strncmp(name, "explicit-", 9)) {
        explicit_scope = expected_ssid = "TEST-WLAN1";
        credential_json = "{\"s\":\"TEST-WLAN1\",\"p\":\"fake-test-password\"}";
        expect_restore = true;
        if (!strcmp(name, "explicit-old-static")) { /* Keep legacy scope. */ }
        else if (!strcmp(name, "explicit-no-static")) static_present = false;
        else if (!strcmp(name, "explicit-invalid-static")) scope.magic[0] = 'X';
        else if (!strcmp(name, "explicit-mismatch")) {
            credential_json = "{\"s\":\"BreathOn-Lab\",\"p\":\"fake-test-password\"}"; expect_restore = false;
        }
        else if (!strcmp(name, "explicit-empty")) { explicit_scope = ""; expect_restore = false; }
        else if (!strcmp(name, "explicit-long")) {
            explicit_scope = "123456789012345678901234567890123"; explicit_scope_length = 33; expect_restore = false;
        }
        else if (!strcmp(name, "explicit-nul")) {
            explicit_scope = "TEST-WLAN1\0extra"; explicit_scope_length = 16; expect_restore = false;
        }
        else if (!strcmp(name, "explicit-read-error")) { scope_read_result = OPRT_COM_ERROR; expect_restore = false; }
        else if (!strcmp(name, "explicit-presentation")) { provider = false; }
        else if (!strcmp(name, "explicit-tuya")) { provider = false; expect_restore = false; }
        else if (!strcmp(name, "explicit-tuya-after-marker")) expect_restore = false;
        else if (!strcmp(name, "explicit-requested-same")) { strcpy(connected.ssid, explicit_scope); expect_restore = false; }
        else if (!strcmp(name, "explicit-reconfigure-during-stop")) { reconfigure_during_stop = true; expect_restore = false; }
        else { fprintf(stderr, "Unknown explicit-scope test case\n"); exit(2); }
        /* Invalid explicit records must not silently fall back to legacy. */
        if (!strcmp(name, "explicit-empty") || !strcmp(name, "explicit-long") ||
            !strcmp(name, "explicit-nul") || !strcmp(name, "explicit-read-error"))
            credential_json = "{\"s\":\"BreathOn-Lab\",\"p\":\"fake-test-password\"}";
    }
    else if (!strcmp(name, "no-static-no-explicit")) static_present = false;
    else if (!strcmp(name, "unbound")) expect_restore=true;
    else if (!strcmp(name, "handoff-race")) handoff_race = true;
    else if (!strcmp(name, "static-actual-mismatch")) {
        station = WSS_CONN_SUCCESS; strcpy(connected.ssid, scope.ssid);
        strcpy(associated_ssid, "TEST-WLAN1");
    }
    else if (!strcmp(name, "static-actual-error")) {
        station = WSS_CONN_SUCCESS; strcpy(connected.ssid, scope.ssid);
        strcpy(associated_ssid, scope.ssid); association_error = true;
    }
    else if (!strcmp(name, "parse-no-allocations")) { expect_restore = true; check_json_allocations = true; }
    else if (!strcmp(name, "parse-failure-no-allocations")) {
        check_json_allocations = true;
        credential_json = "{\"s\":\"BreathOn-Lab\",\"p\":\"fake-test-password\\q\"}";
    }
    else if (!strcmp(name, "tuya")) provider=false;
    else if (!strcmp(name, "tuya-after-marker")) { /* Change later. */ }
    else if (!strcmp(name, "mismatched-saved-ssid")) credential_json="{\"s\":\"Other\",\"p\":\"fake-test-password\"}";
    else if (!strcmp(name, "requested-other")) strcpy(connected.ssid, "Other");
    else if (!strcmp(name, "requested-same")) strcpy(connected.ssid, scope.ssid);
    else if (!strcmp(name, "reconfigure-during-stop")) reconfigure_during_stop=true;
    else if (!strcmp(name, "connecting")) station=WSS_CONNECTING;
    else if (!strcmp(name, "linked")) { station=WSS_GOT_IP; link=NETMGR_LINK_UP; }
    else if (!strcmp(name, "missing-netinfo")) credential_json=NULL;
    else if (!strcmp(name, "non-string-password")) credential_json="{\"s\":\"BreathOn-Lab\",\"p\":123}";
    else if (!strcmp(name, "missing-password")) credential_json="{\"s\":\"BreathOn-Lab\"}";
    else if (!strcmp(name, "empty-ssid")) credential_json="{\"s\":\"\",\"p\":\"fake-test-password\"}";
    else if (!strcmp(name, "long-ssid")) credential_json="{\"s\":\"12345678901234567890123456789012345\",\"p\":\"fake-test-password\"}";
    else if (!strcmp(name, "long-password")) credential_json="{\"s\":\"BreathOn-Lab\",\"p\":\"12345678901234567890123456789012345678901234567890123456789012345678901\"}";
    else if (!strcmp(name, "trailing-junk")) credential_json="{\"s\":\"BreathOn-Lab\",\"p\":\"fake-test-password\"}false";
    else if (!strcmp(name, "duplicate-key")) credential_json="{\"s\":\"BreathOn-Lab\",\"p\":\"fake-test-password\",\"p\":\"extra\"}";
    else if (!strcmp(name, "encoded-nul")) credential_json="{\"s\":\"BreathOn-Lab\\u0000extra\",\"p\":\"fake-test-password\"}";
    else if (!strcmp(name, "embedded-nul")) {
        static const char record[]="{\"s\":\"BreathOn-Lab\",\"p\":\"fake-test-password\"}\0extra";
        credential_json=record; credential_length=sizeof(record)-1;
    }
    else if (!strcmp(name, "invalid-scope")) scope.magic[0]='X';
    else if (!strcmp(name, "other-static-ssid")) { station=WSS_CONN_SUCCESS; strcpy(connected.ssid, "Other"); }
    else if (!strcmp(name, "tuya-static-ssid")) { provider=false; station=WSS_CONN_SUCCESS; strcpy(connected.ssid, scope.ssid); }
    else if (!strcmp(name, "nested-json")) credential_json="{\"s\":\"BreathOn-Lab\",\"p\":{\"value\":\"fake-test-password\"}}";
    else if (!strcmp(name, "escaped-password")) { expect_restore=true; credential_json="{\"s\":\"BreathOn-Lab\",\"p\":\"quoted\\\"slash\\\\u0000\"}"; }
    else if (!strcmp(name, "open-network")) { expect_restore=true; credential_json="{\"s\":\"BreathOn-Lab\",\"p\":\"\"}"; }
    else if (!strcmp(name, "trailing-space")) { expect_restore=true; credential_json="{\"s\":\"BreathOn-Lab\",\"p\":\"fake-test-password\"}\r\n "; }
    else { fprintf(stderr, "Unknown test case\n"); exit(2); }
}
static void invoke_save(const char *ssid, const char *password)
{
    char *args[] = {"breathon_wifi_save", (char *)ssid, (char *)password};
    for (unsigned i = 0; i < command_count; ++i) {
        if (!strcmp(commands[i]->name, args[0])) {
            saving = true; commands[i]->function(NULL, 0, 3, args); saving = false; return;
        }
    }
    assert(false);
}
static void test_save(const char *name)
{
    const char *ssid = "TEST-WLAN1", *password = "fake-test-password";
    bool reject = false;
    if (!strcmp(name, "save-max-ssid")) ssid = "12345678901234567890123456789012";
    if (!strcmp(name, "save-open-network")) password = "";
    if (!strcmp(name, "save-escaped")) { ssid = "Quoted\"\\Lab"; password = "fake\"\\\n\t\001-test"; }
    strcpy(connected.ssid, ssid); strcpy(connected.pswd, password);
    strcpy(associated_ssid, ssid);
    station = WSS_GOT_IP; link = NETMGR_LINK_UP;
    if (!strcmp(name, "save-current-mismatch")) { strcpy(connected.ssid, "BreathOn-Lab"); reject = true; }
    if (!strcmp(name, "save-actual-mismatch")) { strcpy(associated_ssid, "BreathOn-Lab"); reject = true; }
    if (!strcmp(name, "save-actual-error")) { association_error = true; reject = true; }
    if (!strcmp(name, "save-actual-unterminated")) { memset(associated_ssid, 'N', sizeof(associated_ssid)); reject = true; }
    if (!strcmp(name, "save-no-allocations")) check_json_allocations = true;
    if (!strcmp(name, "save-current-error")) { ssid_read_result = OPRT_COM_ERROR; reject = true; }
    if (!strcmp(name, "save-current-unterminated")) { memset(connected.ssid, 'N', sizeof(connected.ssid)); reject = true; }
    if (!strcmp(name, "save-no-link")) { link = NETMGR_LINK_DOWN; reject = true; }
    if (!strcmp(name, "save-no-ip-state")) { station = WSS_CONNECTING; reject = true; }
    if (!strcmp(name, "save-netinfo-failure")) netinfo_write_result = OPRT_COM_ERROR;
    if (!strcmp(name, "save-scope-failure")) scope_write_result = OPRT_COM_ERROR;
    assert(app_breathon_wifi_register() == 0);
    invoke_save(ssid, password);
    if (reject) assert(netinfo_writes == 0 && scope_writes == 0 && connections == 0);
    else {
        assert(netinfo_writes == 1);
        assert(scope_writes == (netinfo_write_result == OPRT_OK ? 1 : 0));
        if (netinfo_write_result == OPRT_OK && scope_write_result == OPRT_OK) {
            assert(!strcmp(written_scope, ssid));
            netconn_wifi_info_t decoded = {0};
            assert(wifi_json_parse((const uint8_t *)written_netinfo, strlen(written_netinfo), &decoded));
            assert(!strcmp(decoded.ssid, ssid) && !strcmp(decoded.pswd, password));
            wifi_secret_wipe(&decoded, sizeof(decoded));
        } else {
            assert(connections == 0);
            assert(!strstr(log_buffer, "persist result=0"));
        }
    }
    assert(stops == 0 && paid_starts == 0);
    if (check_json_allocations) assert(json_allocations == 0);
    if (password[0]) assert(!strstr(log_buffer, password));
    assert(!strstr(log_buffer, ssid));
    printf("PASS Wi-Fi save: %s; verified SSID-only scope\n", name);
}
static bool all_zero(const void *data, size_t size)
{
    const uint8_t *p = data;
    for (size_t i = 0; i < size; ++i) if (p[i]) return false;
    return true;
}
static void test_codec(void)
{
    static const struct { const char *json, *password; } valid[] = {
        {"{\"s\":\"Lab\",\"p\":\"\\\"\\\\\\/\\b\\f\\n\\r\\t\"}", "\"\\/\b\f\n\r\t"},
        {"{\"p\":\"\\u0041\\u00e9\\u4e2d\\ud83d\\ude00\",\"s\":\"Lab\"}", "A\xc3\xa9\xe4\xb8\xad\xf0\x9f\x98\x80"},
        {" {\"\\u0073\":\"Lab\",\"p\":\"\"} \n", ""}
    };
    netconn_wifi_info_t decoded = {0}, roundtrip = {0};
    char buffer[WIFI_JSON_CAPACITY]; size_t length;
    for (unsigned i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        assert(wifi_json_parse((const uint8_t *)valid[i].json, strlen(valid[i].json), &decoded));
        assert(!strcmp(decoded.ssid, "Lab") && !strcmp(decoded.pswd, valid[i].password));
        assert(wifi_json_encode(&decoded, buffer, sizeof(buffer), &length));
        assert(wifi_json_parse((const uint8_t *)buffer, length, &roundtrip));
        assert(!memcmp(&decoded, &roundtrip, sizeof(decoded)));
    }
    static const char *invalid[] = {
        "{\"s\":\"Lab\",\"p\":\"secret\\u0000tail\"}",
        "{\"s\":\"Lab\",\"p\":\"secret\\ud800\"}",
        "{\"s\":\"Lab\",\"p\":\"secret\\udc00\"}",
        "{\"s\":\"Lab\",\"p\":\"secret\\ud800\\u0041\"}",
        "{\"s\":\"Lab\",\"p\":\"secret\\u12xy\"}",
        "{\"s\":\"Lab\",\"p\":\"secret\\q\"}",
        "{\"s\":\"Lab\",\"p\":\"secret\nraw\"}",
        "{\"s\":\"Lab\",\"p\":\"secret\",\"s\":\"Other\"}",
        "{\"s\":\"Lab\",\"p\":\"secret\",}",
        "{\"s\":\"Lab\",\"p\":\"secret\"}x"
    };
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        memset(&decoded, 0xa5, sizeof(decoded));
        assert(!wifi_json_parse((const uint8_t *)invalid[i], strlen(invalid[i]), &decoded));
        assert(all_zero(&decoded, sizeof(decoded)));
    }
    /* Every prefix exercises a truncation exit after arbitrary decoded bytes. */
    for (size_t n = 0; n < strlen(valid[0].json); ++n) {
        assert(!wifi_json_parse((const uint8_t *)valid[0].json, n, &decoded));
        assert(all_zero(&decoded, sizeof(decoded)));
    }
    memset(decoded.ssid, 1, WIFI_SSID_LEN); decoded.ssid[WIFI_SSID_LEN] = 0;
    memset(decoded.pswd, 2, WIFI_PASSWD_LEN); decoded.pswd[WIFI_PASSWD_LEN] = 0;
    assert(wifi_json_encode(&decoded, buffer, sizeof(buffer), &length));
    size_t needed = length + 1;
    assert(wifi_json_parse((const uint8_t *)buffer, length, &roundtrip));
    assert(!memcmp(&decoded, &roundtrip, sizeof(decoded)));
    for (size_t n = 0; n < needed; ++n) {
        memset(buffer, 0xa5, sizeof(buffer));
        assert(!wifi_json_encode(&decoded, buffer, n, &length));
        assert(length == 0 && all_zero(buffer, n));
    }
    wifi_secret_wipe(buffer, sizeof(buffer));
    wifi_secret_wipe(&decoded, sizeof(decoded)); wifi_secret_wipe(&roundtrip, sizeof(roundtrip));
    assert(json_allocations == 0);
    printf("PASS Wi-Fi codec: escapes, Unicode, limits, truncation and failure wiping; zero allocations\n");
}
static HANDLE request_start, request_entered;
static DWORD WINAPI newer_request_thread(void *unused)
{
    (void)unused;
    assert(WaitForSingleObject(request_start, 5000) == WAIT_OBJECT_0);
    netconn_wifi_info_t newer = {0}; strcpy(newer.ssid, "Newer-explicit");
    SetEvent(request_entered);
    assert(netmgr_conn_set(NETCONN_WIFI, NETCONN_CMD_SSID_PSWD, &newer) == OPRT_OK);
    return 0;
}
static bool allow_while_newer_request_waits(void)
{
    assert(!irq_depth && request_lock_depth == 1);
    SetEvent(request_start);
    assert(WaitForSingleObject(request_entered, 5000) == WAIT_OBJECT_0);
    return true;
}
static void test_mutex_handoff(void)
{
    request_start = CreateEvent(NULL, TRUE, FALSE, NULL);
    request_entered = CreateEvent(NULL, TRUE, FALSE, NULL);
    assert(request_start && request_entered);
    HANDLE thread = CreateThread(NULL, 0, newer_request_thread, NULL, 0, NULL); assert(thread);
    started_modules = 2; stops = 1;
    netconn_wifi_info_t old = {0}; strcpy(old.ssid, "Boot-restore");
    assert(netconn_wifi_restore_if_idle(&old, allow_while_newer_request_waits) == OPRT_OK);
    assert(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    assert(connections == 2 && !strcmp(requested.ssid, "Newer-explicit"));
    assert(!strcmp(connected.ssid, "Newer-explicit") && !irq_depth && !request_lock_depth);
    CloseHandle(thread); CloseHandle(request_start); CloseHandle(request_entered);
    printf("PASS Wi-Fi mutex: newer explicit request wins at submission; SDK calls run with IRQs enabled\n");
}
static DWORD WINAPI lifecycle_first_thread(void *unused)
{
    (void)unused;
    if (lifecycle_first_type)
        assert(netconn_wifi_stop_config_type(lifecycle_first_type) == OPRT_OK);
    else app_breathon_wifi_restore_boot(scope.ssid);
    assert(!irq_depth && !request_lock_depth && !netcfg_lock_depth);
    return 0;
}
static DWORD WINAPI lifecycle_newer_thread(void *unused)
{
    (void)unused;
    lifecycle_secondary = true;
    if (lifecycle_explicit) {
        char *args[] = {"breathon_wifi_connect", "Newer-explicit", "fake-test-password"};
        bool found = false;
        for (unsigned i = 0; i < command_count; ++i) {
            if (!strcmp(commands[i]->name, args[0])) {
                commands[i]->function(NULL, 0, 3, args); found = true; break;
            }
        }
        assert(found);
    } else {
        /* SDK netcfg_finish claims and installs its request before stopping
         * provisioning, then submits once the stop completes. */
        assert(wifi_request_begin(true) == OPRT_OK);
        strcpy(connected.ssid, "Newer-sdk");
        wifi_request_end();
        lifecycle_second_result = lifecycle_bypass ? netcfg_stop(0) : netconn_wifi_stop_config();
        if (lifecycle_bypass) {
            SetEvent(stop_second_attempt);
            assert(WaitForSingleObject(stop_first_release, 5000) == WAIT_OBJECT_0);
        }
        assert(wifi_request_begin(false) == OPRT_OK);
        assert(__netconn_wifi_connect() == OPRT_OK);
        wifi_request_end();
    }
    assert(!irq_depth && !request_lock_depth && !netcfg_lock_depth);
    return 0;
}
static void test_stop_lifecycle(const char *name)
{
    lifecycle_test = true;
    lifecycle_bypass = !strcmp(name, "stop-lifecycle-unsafe-sdk");
    lifecycle_explicit = !strcmp(name, "stop-lifecycle-explicit");
    lifecycle_first_type = !strcmp(name, "stop-lifecycle-typed-ap") ? 1 :
                           !strcmp(name, "stop-lifecycle-typed-ble") ? 2 : 0;
    cfg_session.live = cfg_session.started[0] = cfg_session.started[1] = true;
    InitializeCriticalSection(&lifecycle_lock);
    stop_first_blocked = CreateEvent(NULL, TRUE, FALSE, NULL);
    stop_first_release = CreateEvent(NULL, TRUE, FALSE, NULL);
    stop_second_attempt = CreateEvent(NULL, TRUE, FALSE, NULL);
    assert(stop_first_blocked && stop_first_release && stop_second_attempt);
    assert(app_breathon_wifi_register() == OPRT_OK);
    token_pending(); drain_queue();
    HANDLE first = CreateThread(NULL, 0, lifecycle_first_thread, NULL, 0, NULL); assert(first);
    assert(WaitForSingleObject(stop_first_blocked, 5000) == WAIT_OBJECT_0);
    EnterCriticalSection(&lifecycle_lock);
    assert(cfg_session.live && cfg_session.calls == 1 && cfg_session.callbacks == 1);
    assert(!cfg_session.started[lifecycle_first_type == 2 ? 1 : 0]);
    LeaveCriticalSection(&lifecycle_lock);
    HANDLE newer = CreateThread(NULL, 0, lifecycle_newer_thread, NULL, 0, NULL); assert(newer);
    assert(WaitForSingleObject(stop_second_attempt, 5000) == WAIT_OBJECT_0);
    /* The new owner must claim while the first task is blocked in AP stop. */
    assert(wifi_request_begin(false) == OPRT_OK);
    assert(wifi_request_owned); wifi_request_end();
    EnterCriticalSection(&lifecycle_lock);
    bool preserved = cfg_session.live && cfg_session.calls == 1 && cfg_session.frees == 0;
    LeaveCriticalSection(&lifecycle_lock);
    SetEvent(stop_first_release);
    assert(WaitForSingleObject(first, 5000) == WAIT_OBJECT_0);
    assert(WaitForSingleObject(newer, 5000) == WAIT_OBJECT_0);
    CloseHandle(first); CloseHandle(newer);
    CloseHandle(stop_first_blocked); CloseHandle(stop_first_release); CloseHandle(stop_second_attempt);
    DeleteCriticalSection(&lifecycle_lock);
    /* An unsafe run fails with concrete lifecycle counts, without causing an
     * actual host use-after-free or relying on thread scheduling sleeps. */
    if (!preserved || cfg_session.frees != 1 || cfg_session.after_free) {
        fprintf(stderr, "FAIL %s: preserved=%d raw_calls=%u frees=%u after_free=%u\n",
                name, preserved, cfg_session.calls, cfg_session.frees, cfg_session.after_free);
        exit(1);
    }
    assert(cfg_session.calls == 2 && cfg_session.callbacks == 2 && !cfg_session.live);
    if (!lifecycle_explicit)
        assert(lifecycle_second_result == (lifecycle_first_type ? OPRT_OK : OPRT_COM_ERROR));
    if (lifecycle_first_type) app_breathon_wifi_restore_boot(scope.ssid);
    assert(connections == 1 && !strcmp(requested.ssid, lifecycle_explicit ? "Newer-explicit" : "Newer-sdk"));
    assert(stops == 2 && paid_starts == 0);
    assert(!strstr(log_buffer, "fake-test-password"));
    printf("PASS Wi-Fi provisioning lifecycle: %s; one free, no after-free traversal, new owner wins, IRQs enabled\n", name);
}
static void test_cancel(const char *name)
{
    assert(app_breathon_wifi_register() == 0);
    token_pending();
    if (!strcmp(name, "cancel-after-marker")) drain_queue();
    strcpy(connected.ssid, "TEST-WLAN1"); station = WSS_GOT_IP; link = NETMGR_LINK_UP;
    strcpy(associated_ssid, "TEST-WLAN1");
    invoke_save("TEST-WLAN1", "fake-test-password");
    assert(scope_writes == 1);
    connections = 0; memset(&connected, 0, sizeof(connected)); station = WSS_IDLE; link = NETMGR_LINK_DOWN;
    drain_queue(); tick(); tick();
    assert(connections == 0 && stops == 0 && paid_starts == 0);
    printf("PASS Wi-Fi cancellation: %s\n", name);
}
/* Keep the existing PowerShell entry point: each added case gets fresh C
 * module state in a child host process, just like the original runner cases. */
static void extra_cases(const char *executable)
{
    static const char *cases[] = {
        "explicit-old-static", "explicit-no-static", "explicit-invalid-static", "explicit-mismatch",
        "explicit-empty", "explicit-long", "explicit-nul", "explicit-read-error", "no-static-no-explicit",
        "explicit-tuya", "explicit-tuya-after-marker", "explicit-requested-same", "explicit-reconfigure-during-stop",
        "save-success", "save-current-mismatch", "save-current-error", "save-current-unterminated", "save-no-link",
        "save-no-ip-state", "save-netinfo-failure", "save-scope-failure", "save-open-network", "save-max-ssid",
        "cancel-before-marker", "cancel-after-marker",
        "save-actual-mismatch", "save-actual-error", "save-actual-unterminated", "static-actual-mismatch",
        "handoff-race", "save-no-allocations", "parse-no-allocations", "parse-failure-no-allocations",
        "save-escaped", "codec", "mutex-handoff", "static-actual-error",
        "stop-lifecycle-explicit", "stop-lifecycle-sdk", "stop-lifecycle-typed-ap", "stop-lifecycle-typed-ble"
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const char *args[] = {executable, cases[i], NULL};
        assert(_spawnv(_P_WAIT, executable, args) == 0);
    }
}
int main(int argc, char **argv)
{
    const char *name = argc > 1 ? argv[1] : "unbound";
    cJSON_Hooks hooks = {count_json_alloc, free}; cJSON_InitHooks(&hooks);
    assert(wifi_request_init() == OPRT_OK);
    if (!strcmp(name, "codec")) { test_codec(); return 0; }
    if (!strcmp(name, "mutex-handoff")) { test_mutex_handoff(); return 0; }
    if (!strncmp(name, "stop-lifecycle-", 15)) { test_stop_lifecycle(name); return 0; }
    if (!strcmp(name, "unbound")) extra_cases(argv[0]);
    if (!strncmp(name, "save-", 5)) { test_save(name); return 0; }
    if (!strncmp(name, "cancel-", 7)) { test_cancel(name); return 0; }
    select_case(name);
    assert(app_breathon_wifi_register()==0);
    tick(); assert(connections==0 && stops==0);
    token_pending(); tick(); assert(connections==0 && stops==0);
    drain_queue();
    if (!strcmp(name, "tuya-after-marker") || !strcmp(name, "explicit-tuya-after-marker")) provider=false;
    tick();
    if (expect_restore && connections != 1) {
        fprintf(stderr, "FAIL %s: expected one saved Wi-Fi connection after SDK provisioning startup; actual=%d\n", name, connections);
        return 1;
    }
    if (expect_restore) {
        assert(!strcmp(requested.ssid, expected_ssid));
        const char *password = !strcmp(name, "escaped-password") ? "quoted\"slash\\u0000" :
                               !strcmp(name, "open-network") ? "" : "fake-test-password";
        assert(!strcmp(requested.pswd, password));
        assert(credential_wiped);
        tick(); assert(connections==1);
        strcpy(associated_ssid, requested.ssid);
        station=WSS_CONN_SUCCESS; tick();
        if (!strcmp(expected_ssid, scope.ssid)) assert(static_requests==1 && link==NETMGR_LINK_UP);
        else {
            assert(static_requests == 0); /* Legacy static address must not leak onto DHCP. */
            station = WSS_GOT_IP; link = NETMGR_LINK_UP; tick(); assert(static_requests == 0);
        }
        /* A second token callback cannot schedule a second connect. */
        tuya_iot_config_t config={0}; assert(boot_callback(&config)==0);
        drain_queue(); tick(); assert(connections==1);
    } else {
        if (!strcmp(name, "handoff-race")) {
            assert(connections == 1 && stops == 1 && !strcmp(requested.ssid, "Newer-explicit"));
        } else assert(connections==0 && stops==(reconfigure_during_stop ? 1 : 0) && static_requests==0);
    }
    if (credential_allocation_size) assert(credential_wiped);
    if (check_json_allocations) assert(json_allocations == 0);
    assert(paid_starts==0);
    assert(!strstr(log_buffer, "fake-test-password") && !strstr(log_buffer, "BreathOn-Lab") &&
           !strstr(log_buffer, "TEST-WLAN1"));
    printf("PASS Wi-Fi boot restore: %s; no paid autostart or credential logs\n", name);
    return 0;
}
