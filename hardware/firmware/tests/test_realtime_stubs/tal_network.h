#ifndef TEST_RT_NETWORK_H
#define TEST_RT_NETWORK_H
#include <winsock2.h>
#include "tal_api.h"
typedef fd_set TUYA_FD_SET_T;
typedef int TUYA_ERRNO;
typedef uint32_t TUYA_IP_ADDR_T;
enum { PROTOCOL_TCP, TRANS_RECV, TRANS_SEND, UNW_EAGAIN=-4, UNW_EWOULDBLOCK=-12, UNW_EINTR=-2 };
#define TAL_FD_ZERO FD_ZERO
#define TAL_FD_SET(fd,s) FD_SET((SOCKET)(fd),s)
#define TAL_FD_ISSET(fd,s) FD_ISSET((SOCKET)(fd),s)
int tal_net_select(int, TUYA_FD_SET_T *, TUYA_FD_SET_T *, TUYA_FD_SET_T *, uint32_t);
int tal_net_get_errno(void);
int tal_net_socket_create(int);
int tal_net_set_block(int, bool);
int tal_net_disable_nagle(int);
int tal_net_set_timeout(int, int, int);
int tal_net_connect(int, TUYA_IP_ADDR_T, uint16_t);
int tal_net_send(int, const void *, uint32_t);
int tal_net_recv(int, void *, uint32_t);
int tal_net_close(int);
TUYA_IP_ADDR_T tal_net_str2addr(const char *);
#endif
