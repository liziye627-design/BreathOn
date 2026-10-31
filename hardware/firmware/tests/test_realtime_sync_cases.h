/* Included by the real-worker harness; no separate state-machine substitute. */
static HANDLE owner_race_gate;
typedef struct { breathon_audio_owner_t owner; bool acquired; } owner_racer_t;
static DWORD WINAPI race_for_owner(void *arg)
{
    owner_racer_t *racer=arg;
    assert(WaitForSingleObject(owner_race_gate,3000)==WAIT_OBJECT_0);
    unsigned before=critical_entries;
    racer->acquired=app_breathon_audio_acquire(racer->owner);
    assert(critical_entries==before+1 && !critical_depth);
    return 0;
}
static void test_owner_race(void)
{
    owner_race_gate=CreateEvent(NULL,TRUE,FALSE,NULL); assert(owner_race_gate);
    for(unsigned i=0;i<32;++i) {
        ResetEvent(owner_race_gate);
        owner_racer_t racers[2]={{BREATHON_OWNER_LOCAL,false},{BREATHON_OWNER_REALTIME,false}};
        HANDLE threads[2]={CreateThread(NULL,0,race_for_owner,&racers[0],0,NULL),
                           CreateThread(NULL,0,race_for_owner,&racers[1],0,NULL)};
        assert(threads[0] && threads[1]);
        LONG before=metric(&stops);
        SetEvent(owner_race_gate);
        assert(WaitForMultipleObjects(2,threads,TRUE,3000)==WAIT_OBJECT_0);
        assert(racers[0].acquired != racers[1].acquired);
        unsigned winner=racers[0].acquired?0:1;
        assert(app_breathon_audio_owner()==racers[winner].owner && metric(&stops)==before+1);
        app_breathon_audio_release(racers[1-winner].owner);
        assert(app_breathon_audio_owner()==racers[winner].owner && metric(&stops)==before+1);
        app_breathon_audio_release(racers[winner].owner);
        assert(app_breathon_audio_owner()==BREATHON_OWNER_NONE && metric(&stops)==before+2);
        CloseHandle(threads[0]); CloseHandle(threads[1]);
    }
    CloseHandle(owner_race_gate);
}
static void await_owner(breathon_audio_owner_t owner)
{
    DWORD since=GetTickCount();
    while(app_breathon_audio_owner()!=owner && GetTickCount()-since<3000) Sleep(1);
    assert(app_breathon_audio_owner()==owner);
}
static void assert_no_connection(SOCKET listener)
{
    fd_set fds; FD_ZERO(&fds); FD_SET(listener,&fds); struct timeval tv={0,100000};
    assert(select(0,&fds,NULL,NULL,&tv)==0);
}
static void assert_microphone_pcm(SOCKET peer,bool muted)
{
    uint8_t pcm[RT_FRAME_BYTES],body[RT_PAYLOAD_MAX+1]; memset(pcm,0x26,sizeof(pcm));
    AI_NOTIFY_MIC_DATA_T mic={pcm,sizeof(pcm)}; AI_NOTIFY_EVENT_T event={AI_USER_EVT_MIC_DATA,&mic};
    app_breathon_realtime_event(&event);
    rt_header_t h=receive_frame(peer,body);
    assert(h.kind==RT_MIC && h.generation==0 && h.length==sizeof(pcm));
    for(size_t i=0;i<h.length;++i)assert(body[i]==(muted?0:pcm[i]));
}
static void test_deferred_start_and_mute(SOCKET listener,uint16_t port)
{
    SOCKET peer=open_session(listener);
    LONG connected=metric(&connections);
    assert_microphone_pcm(peer,false);
    /* Mute clears captured backlog. START while already running must neither
     * reconnect nor unmute; verify silence through the actual TCP worker. */
    uint8_t pcm[RT_FRAME_BYTES]; memset(pcm,0x26,sizeof(pcm));
    AI_NOTIFY_MIC_DATA_T mic={pcm,sizeof(pcm)}; AI_NOTIFY_EVENT_T event={AI_USER_EVT_MIC_DATA,&mic};
    uint32_t irq=tal_system_enter_critical();
    app_breathon_realtime_event(&event); assert(rt_current->mic.used==sizeof(pcm));
    assert(app_breathon_realtime_mute(true) && rt_current->mic.used==0);
    app_breathon_realtime_start(); assert(rt_request==2);
    assert(app_breathon_realtime_config("127.0.0.1",port,"synthetic-lan-token")==OPRT_COM_ERROR);
    tal_system_exit_critical(irq);
    assert_microphone_pcm(peer,true);
    assert(metric(&connections)==connected); assert_no_connection(listener);

    for(unsigned remute=0;remute<2;++remute) {
        connected=metric(&connections);
        irq=tal_system_enter_critical();
        assert(app_breathon_realtime_mute(true));
        app_breathon_realtime_stop(); app_breathon_realtime_start(); app_breathon_realtime_start();
        assert(rt_request==3); /* Cleanup stays in charge until the old session ends. */
        if(remute)assert(app_breathon_realtime_mute(true));
        tal_system_exit_critical(irq);
        /* Accept the deferred START without issuing another START: that could
         * conceal a lost restart or reset mute again during STOP cleanup. */
        SOCKET next=accept_session(listener); closesocket(peer); peer=next;
        assert(metric(&connections)==connected+1);
        assert_microphone_pcm(peer,remute!=0);
        LONG stopped=metric(&stops);
        assert(!app_breathon_audio_acquire(BREATHON_OWNER_LOCAL));
        app_breathon_audio_release(BREATHON_OWNER_LOCAL);
        assert(app_breathon_audio_owner()==BREATHON_OWNER_REALTIME && metric(&stops)==stopped);
        assert(app_breathon_realtime_mute(false)); assert_microphone_pcm(peer,false);
        assert_no_connection(listener);
    }
    /* A later explicit STOP cancels the deferred START and returns ownership. */
    connected=metric(&connections);
    irq=tal_system_enter_critical();
    app_breathon_realtime_stop(); app_breathon_realtime_start(); app_breathon_realtime_stop();
    assert(rt_request==3); tal_system_exit_critical(irq);
    await_idle(); closesocket(peer);
    assert(metric(&connections)==connected); assert_no_connection(listener);
}
static void test_sync_contract(SOCKET listener,uint16_t port)
{
    /* Freeze the real network worker at its shared-state boundary. STOP must
     * survive duplicate START and config, including getters under the lock. */
    uint32_t irq=tal_system_enter_critical();
    unsigned before=critical_entries;
    app_breathon_realtime_start();
    assert(critical_entries==before+1 && rt_request==1);
    app_breathon_realtime_start(); assert(rt_request==1);
    app_breathon_realtime_stop(); assert(rt_request==3);
    app_breathon_realtime_start(); app_breathon_realtime_stop(); assert(rt_request==3);
    before=critical_entries;
    assert(rt_stop_requested() && app_breathon_realtime_active());
    assert(critical_entries==before); /* getters must not nest the TAL lock */
    assert(app_breathon_realtime_config("127.0.0.1",port,"synthetic-lan-token")==OPRT_COM_ERROR);
    tal_system_exit_critical(irq);
    await_idle(); assert(metric(&errors)==0);
    app_breathon_realtime_stop(); assert(!app_breathon_realtime_active());

    /* KV I/O occurs without IRQ masking; start/stop cannot steal configuring.
     * A failed save must release the gate so a subsequent save can succeed. */
    InterlockedExchange(&check_config_gate,1); InterlockedExchange(&kv_fail,1);
    assert(app_breathon_realtime_config("127.0.0.1",port,"synthetic-lan-token")==OPRT_COM_ERROR);
    assert(!app_breathon_realtime_active() && !app_breathon_realtime_configured());
    InterlockedExchange(&kv_fail,0);
    assert(app_breathon_realtime_config("127.0.0.1",port,"synthetic-lan-token")==OPRT_OK);
    InterlockedExchange(&check_config_gate,0); await_metric(&configured,1);
    test_owner_race();

    test_deferred_start_and_mute(listener,port);

    /* Queue STOP before the real audio worker starts. Later commands cannot
     * replace it; after consumption, TONE is accepted and STOP releases owner. */
    app_breathon_audio_request(BREATHON_AUDIO_TONE);
    app_breathon_audio_request(BREATHON_AUDIO_STOP);
    app_breathon_audio_request(BREATHON_AUDIO_TONE);
    assert(app_breathon_audio_stop_pending());
    assert(app_breathon_audio_init()==OPRT_OK);
    DWORD since=GetTickCount();
    while(app_breathon_audio_stop_pending() && GetTickCount()-since<3000) Sleep(1);
    assert(!app_breathon_audio_stop_pending() && metric(&tones)==0);
    app_breathon_audio_request(BREATHON_AUDIO_TONE); await_metric(&tones,1);
    assert(app_breathon_audio_owner()==BREATHON_OWNER_LOCAL);
    irq=tal_system_enter_critical();
    app_breathon_audio_request(BREATHON_AUDIO_STATUS);
    app_breathon_audio_request(BREATHON_AUDIO_STOP);
    app_breathon_audio_request(BREATHON_AUDIO_TONE);
    before=critical_entries;
    assert(app_breathon_audio_stop_pending()); assert(critical_entries==before);
    tal_system_exit_critical(irq);
    await_owner(BREATHON_OWNER_NONE); assert(metric(&tones)==1 && !metric(&playing));
    assert(!critical_depth && metric(&errors)==0);
    puts("sync workers passed: deferred START exactly once, STOP cancels restart, mute PCM/reset/preservation, running START idempotence, config gate/failure, nonlocking getters, 32 owner races, audio STOP priority, IRQ-mask restoration and SDK calls outside critical");
}
