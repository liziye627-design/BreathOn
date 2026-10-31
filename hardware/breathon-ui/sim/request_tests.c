#include "../../firmware/realtime_request.h"
#include <assert.h>
int main(void)
{
    bool restart=false, muted=false;
    /* Given a running call stopping asynchronously, a new explicit start
       survives cleanup and becomes exactly one pending session. */
    int state=rt_request_stop_next(2,&restart);
    assert(state==3);
    state=rt_request_start_next(state,&restart,&muted);
    state=rt_request_start_next(state,&restart,&muted);
    assert(state==3 && restart);
    state=rt_request_finish_next(state,&restart);
    assert(state==1 && !restart);
    assert(rt_request_finish_next(2,&restart)==0);
    /* A second hangup cancels the deferred request. */
    state=rt_request_start_next(3,&restart,&muted);
    state=rt_request_stop_next(state,&restart);
    assert(rt_request_finish_next(state,&restart)==0);
    /* Configuring, pending and active calls do not spawn extra sessions. */
    assert(rt_request_start_next(-1,&restart,&muted)==-1 && !restart);
    assert(rt_request_start_next(0,&restart,&muted)==1 && !restart);
    assert(rt_request_start_next(1,&restart,&muted)==1 && !restart);
    assert(rt_request_start_next(2,&restart,&muted)==2 && !restart);
    assert(rt_request_stop_next(-1,&restart)==-1);
    assert(rt_request_stop_next(0,&restart)==0);
    /* Given STOP -> deferred START -> mute, cleanup must retain the user's
       new call privacy choice, the same flag used by the MIC sample gate. */
    state=rt_request_stop_next(2,&restart);
    muted=true;
    state=rt_request_start_next(state,&restart,&muted);
    assert(!muted);
    muted=true;
    state=rt_request_finish_next(state,&restart);
    assert(state==1 && muted);
    assert(rt_request_start_next(1,&restart,&muted)==1 && muted);
    assert(rt_request_start_next(2,&restart,&muted)==2 && muted);
    return 0;
}
