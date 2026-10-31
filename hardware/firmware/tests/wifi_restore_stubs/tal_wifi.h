#ifndef WIFI_RESTORE_TAL_WIFI_H
#define WIFI_RESTORE_TAL_WIFI_H
#include "tal_api.h"
#define WIFI_SSID_LEN 32
#define WIFI_PASSWD_LEN 64
typedef enum { WSS_IDLE, WSS_CONNECTING, WSS_PASSWD_WRONG, WSS_NO_AP_FOUND, WSS_CONN_FAIL, WSS_CONN_SUCCESS, WSS_GOT_IP } WF_STATION_STAT_E;
enum { WF_STATION };
typedef struct { char ip[16], mask[16], gw[16]; } NW_IP_S;
int tal_wifi_station_get_status(WF_STATION_STAT_E *);
int tal_wifi_get_ip(int, NW_IP_S *);
#endif
