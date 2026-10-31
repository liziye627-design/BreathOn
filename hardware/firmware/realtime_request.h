#ifndef BREATHON_REALTIME_REQUEST_H
#define BREATHON_REALTIME_REQUEST_H
#include <stdbool.h>
/* Caller serializes state and restart together. No hardware or callbacks here. */
int rt_request_start_next(int state, bool *restart, bool *muted);
int rt_request_stop_next(int state, bool *restart);
int rt_request_finish_next(int state, bool *restart);
#endif
