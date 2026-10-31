#include "realtime_request.h"
int rt_request_start_next(int state, bool *restart, bool *muted)
{
    /* Reset only at the explicit new START, never later during cleanup:
     * a user can mute the deferred call before it acquires the microphone. */
    if (state == 0 || state == 3) *muted = false;
    if (state == 3) *restart = true;
    return state == 0 ? 1 : state;
}
int rt_request_stop_next(int state, bool *restart)
{
    *restart = false;
    return state > 0 ? 3 : state;
}
int rt_request_finish_next(int state, bool *restart)
{
    int next = state == 3 && *restart ? 1 : 0;
    *restart = false;
    return next;
}
