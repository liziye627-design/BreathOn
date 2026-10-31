#ifndef WIFI_RESTORE_NETCONN_WIFI_H
#define WIFI_RESTORE_NETCONN_WIFI_H
#include "tal_wifi.h"
#include "tuya_iot.h"
typedef enum { NETMGR_LINK_DOWN, NETMGR_LINK_UP } netmgr_status_e;
enum { NETCONN_WIFI, NETCONN_CMD_STATUS, NETCONN_CMD_SSID_PSWD };
typedef struct { char ssid[WIFI_SSID_LEN+1], pswd[WIFI_PASSWD_LEN+1]; } netconn_wifi_info_t;
int netmgr_conn_get(int, int, void *);
int netmgr_conn_set(int, int, void *);
int netconn_wifi_restore_if_idle(const netconn_wifi_info_t *, bool (*)(void));
void netconn_wifi_cancel_restore(void);
int netconn_wifi_stop_config(void);
int netconn_wifi_stop_config_type(int type);
int netconn_wifi_run_if_current(const char *, int (*)(void *), void *);
#endif
