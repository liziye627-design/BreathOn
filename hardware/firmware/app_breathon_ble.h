#ifndef APP_BREATHON_BLE_H
#define APP_BREATHON_BLE_H
#include "tal_api.h"

/*
 * BREATHON BLE peripheral (protocol: breathon_ble_proto.h).
 *
 * The Tuya SDK keeps owning the BLE stack for network provisioning. Once WiFi is
 * link-up (or `breathon_ble take` is run) this module takes over the TAL event
 * stream through the breathon_ble_intercept() hook patched into tal_bluetooth.c,
 * advertises as "BREATHON" on the common service 0xFD50 and serves the app.
 * The stack is never restarted: the pinned SDK's deinit/init path is not reusable.
 *
 * No acetone sensor is fitted yet: readings are generated on the board (unflagged).
 */
OPERATE_RET app_breathon_ble_init(void);
bool app_breathon_ble_owned(void);
bool app_breathon_ble_connected(void);
void app_breathon_ble_status(void);
#endif
