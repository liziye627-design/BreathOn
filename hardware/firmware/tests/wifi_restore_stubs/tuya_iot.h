#ifndef WIFI_RESTORE_TUYA_IOT_H
#define WIFI_RESTORE_TUYA_IOT_H
#include "tal_api.h"
typedef struct { int unused; } tuya_iot_config_t;
typedef struct { bool is_activated; } tuya_iot_client_t;
typedef int (*tuya_token_get_cb_t)(tuya_iot_config_t *);
tuya_iot_client_t *tuya_iot_client_get(void);
int tuya_iot_token_get_port_register(tuya_iot_client_t *, tuya_token_get_cb_t);
#endif
