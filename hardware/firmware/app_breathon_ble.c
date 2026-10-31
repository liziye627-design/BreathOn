#include "app_breathon_ble.h"
#include "breathon_ble_proto.h"
#include "tal_bluetooth.h"
#include "netconn_wifi.h"
#include <stdlib.h>
#include <string.h>

/* Pinned Beken CLI ABI, same as app_breathon_realtime_cli.c. */
struct cli_command { const char *name, *help; void (*function)(char *, int, int, char **); };
extern int cli_register_command(const struct cli_command *command);
/* Tuya BLE manager: true while its own app session is paired. Weak: absent without BLE. */
extern bool tuya_ble_is_connected(void) __attribute__((weak));

#define ADV_REASSERT_TICKS 20 /* re-apply our advertising every 5 s while idle */

static bble_dev_t s_dev;
static TIMER_ID s_timer;
static volatile bool s_owned;
static volatile bool s_connected;
static volatile bool s_subscribed;
static volatile bool s_readv; /* advertising must be (re)applied on next tick */
static uint16_t s_mock_x100;  /* 0 = random per run */
static uint32_t s_sent, s_send_fail, s_writes;
static uint16_t s_adv_ticks;

static uint16_t next_target(void)
{
    if (s_mock_x100) return s_mock_x100;
    return (uint16_t)(30 + tal_system_get_random(150)); /* 0.30-1.80 ppm, same range as the app mock */
}

static void adv_apply(void)
{
    static uint8_t adv[] = {
        0x02, 0x01, 0x06,                                    /* flags: LE general, BR/EDR off */
        0x09, 0x09, 'B', 'R', 'E', 'A', 'T', 'H', 'O', 'N', /* complete local name */
        0x03, 0x03, 0x50, 0xFD,                              /* 16-bit service 0xFD50 */
    };
    TAL_BLE_DATA_T adv_data = {.len = sizeof(adv), .p_data = adv};
    TAL_BLE_ADV_PARAMS_T params = DEFAULT_ADV_PARAMS(160, 320); /* 100-200 ms */
    tal_ble_advertising_stop();
    OPERATE_RET rt = tal_ble_advertising_data_set(&adv_data, NULL);
    if (rt == OPRT_OK) rt = tal_ble_advertising_start(&params);
    if (rt != OPRT_OK) PR_ERR("BREATHON_BLE adv failed %d", rt);
}

/* Called by the patched tal_bluetooth.c on the BLE host task. Return non-zero to consume. */
int breathon_ble_intercept(TAL_BLE_EVT_PARAMS_T *evt)
{
    if (!s_owned || evt == NULL) return 0;
    switch (evt->type) {
    case TAL_BLE_STACK_INIT:
    case TAL_BLE_STACK_DEINIT:
        return 0; /* stack lifecycle stays visible to the Tuya manager */
    case TAL_BLE_EVT_PERIPHERAL_CONNECT:
        s_connected = evt->ble_event.connect.result == 0;
        s_subscribed = false;
        if (!s_connected) s_readv = true;
        break;
    case TAL_BLE_EVT_DISCONNECT:
        s_connected = false;
        s_subscribed = false;
        s_readv = true;
        break;
    case TAL_BLE_EVT_SUBSCRIBE:
        s_subscribed = evt->ble_event.subscribe.cur_notify;
        break;
    case TAL_BLE_EVT_WRITE_REQ: {
        uint16_t target = next_target();
        uint32_t irq = tal_system_enter_critical();
        bble_handle_write(&s_dev, evt->ble_event.write_report.report.p_data, evt->ble_event.write_report.report.len,
                          target);
        tal_system_exit_critical(irq);
        s_writes++;
    } break;
    default:
        break; /* MTU, conn params, notify results: nothing for the Tuya manager either */
    }
    return 1;
}

static void tick_cb(TIMER_ID timer_id, void *arg)
{
    (void)timer_id;
    (void)arg;
    uint8_t frame[20];
    uint32_t irq = tal_system_enter_critical();
    size_t len = bble_tick(&s_dev, frame, sizeof(frame));
    tal_system_exit_critical(irq);

    if (len && s_connected && s_subscribed) {
        TAL_BLE_DATA_T data = {.len = (uint16_t)len, .p_data = frame};
        if (tal_ble_server_common_send(&data) == OPRT_OK) {
            s_sent++;
        } else {
            s_send_fail++;
        }
    }
    /* Tuya's cloud monitor may stop advertising behind our back; re-assert while idle. */
    if (!s_connected && (s_readv || ++s_adv_ticks >= ADV_REASSERT_TICKS)) {
        s_readv = false;
        s_adv_ticks = 0;
        adv_apply();
    }
}

static void take_over(void *data)
{
    (void)data;
    if (s_owned) return;
    if (tuya_ble_is_connected && tuya_ble_is_connected()) {
        PR_NOTICE("BREATHON_BLE waiting: Tuya app session active");
        return; /* next link event or `breathon_ble take` retries */
    }
    s_owned = true;
    s_readv = true;
    tal_sw_timer_start(s_timer, BBLE_TICK_MS, TAL_TIMER_CYCLE);
    PR_NOTICE("BREATHON_BLE advertising as BREATHON");
}

static int on_link(void *data)
{
    if (data != NULL && *(netmgr_status_e *)data == NETMGR_LINK_UP && !s_owned) {
        tal_workq_schedule(WORKQ_SYSTEM, take_over, NULL);
    }
    return OPRT_OK;
}

void app_breathon_ble_status(void)
{
    PR_NOTICE("BREATHON_BLE owned=%d connected=%d subscribed=%d state=%s ppm=%u.%02u sent=%u fail=%u writes=%u",
              s_owned, s_connected, s_subscribed, bble_state_name(s_dev.state), s_dev.ppm_x100 / 100,
              s_dev.ppm_x100 % 100, (unsigned)s_sent, (unsigned)s_send_fail, (unsigned)s_writes);
}

static void command(char *output, int size, int argc, char **argv)
{
    (void)output;
    (void)size;
    if (argc == 2 && !strcmp(argv[1], "status")) {
        app_breathon_ble_status();
        return;
    }
    if (argc == 2 && !strcmp(argv[1], "take")) {
        tal_workq_schedule(WORKQ_SYSTEM, take_over, NULL);
        return;
    }
    if (argc == 3 && !strcmp(argv[1], "mock")) {
        double ppm = strtod(argv[2], NULL);
        s_mock_x100 = (ppm > 0 && ppm <= 5.0) ? (uint16_t)(ppm * 100 + 0.5) : 0;
        PR_NOTICE("BREATHON_BLE mock target %s", s_mock_x100 ? "fixed" : "random");
        return;
    }
    PR_NOTICE("breathon_ble status | take | mock <ppm 0.01-5, 0=random>");
}

bool app_breathon_ble_owned(void) { return s_owned; }
bool app_breathon_ble_connected(void) { return s_connected; }

OPERATE_RET app_breathon_ble_init(void)
{
    OPERATE_RET rt = OPRT_OK;
    bble_init(&s_dev, BBLE_BATTERY_UNKNOWN);
    TUYA_CALL_ERR_RETURN(tal_sw_timer_create(tick_cb, NULL, &s_timer));
    TUYA_CALL_ERR_RETURN(tal_event_subscribe(EVENT_LINK_STATUS_CHG, "breathon_ble", on_link, SUBSCRIBE_TYPE_NORMAL));
    static const struct cli_command entry = {"breathon_ble", "BREATHON BLE link", command};
    if (cli_register_command(&entry) != 0) PR_ERR("BREATHON_BLE cli register failed");

    netmgr_status_e status = NETMGR_LINK_DOWN;
    if (netmgr_conn_get(NETCONN_WIFI, NETCONN_CMD_STATUS, &status) == OPRT_OK && status == NETMGR_LINK_UP) {
        tal_workq_schedule(WORKQ_SYSTEM, take_over, NULL);
    }
    return rt;
}
