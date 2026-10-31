#include "tal_api.h"
typedef enum { NETMGR_LINK_DOWN, NETMGR_LINK_UP } netmgr_status_e;
enum { NETCONN_WIFI, NETCONN_CMD_STATUS };
OPERATE_RET netmgr_conn_get(int, int, netmgr_status_e *);
