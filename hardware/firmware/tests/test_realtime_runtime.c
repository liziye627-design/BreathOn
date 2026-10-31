/* Windows localhost integration: real firmware workers, TCP and cJSON; fake
 * board/RTOS adapter and a controllably blocked player. No hardware or API key. */
#include "tal_network.h"
#include <windows.h>
#include <assert.h>
#include "app_breathon_realtime_internal.h"
#include "app_breathon_audio.h"
#include "netconn_wifi.h"
#include "cJSON.h"
/* Report failures without a Windows CRT abort dialog. */
#undef assert
#define assert(ok) do { if (!(ok)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#ok); exit(1); } } while (0)

static CRITICAL_SECTION critical, player_lock;
static HANDLE feed_entered, feed_release;
static volatile LONG listening, configured, errors, feeds, starts, eof_count, stops, hold_feed, wifi_up=1, kws=1;
static volatile LONG playing;
static volatile LONG tones, kv_fail, check_config_gate;
static volatile LONG connections;
static _Thread_local unsigned critical_depth, critical_entries;
static uint8_t kv[151]; static size_t kv_size;
static LONG metric(volatile LONG *p) { return InterlockedCompareExchange(p,0,0); }
static void await_metric(volatile LONG *p, LONG n)
{
    DWORD since=GetTickCount();
    while(metric(p)<n && GetTickCount()-since<3000) Sleep(1);
    assert(metric(p)>=n);
}
static void await_idle(void)
{
    DWORD since=GetTickCount();
    while(app_breathon_realtime_active() && GetTickCount()-since<3000) Sleep(1);
    if(app_breathon_realtime_active()) {
        uint32_t irq=tal_system_enter_critical();
        int request=rt_request; bool present=rt_current!=NULL,ready=present && rt_current->ready;
        tal_system_exit_critical(irq);
        fprintf(stderr,"idle timeout: request=%d session=%d ready=%d owner=%d connections=%ld\n",
                request,present,ready,app_breathon_audio_owner(),metric(&connections));
    }
    assert(!app_breathon_realtime_active());
    assert(app_breathon_audio_owner()==BREATHON_OWNER_NONE);
    assert(rt_current==NULL);
}
uint32_t tal_system_enter_critical(void)
{
    EnterCriticalSection(&critical);
    uint32_t saved = critical_depth ? 0xa5000001u : 0xa5000000u;
    ++critical_depth; ++critical_entries;
    return saved;
}
void tal_system_exit_critical(uint32_t irq)
{
    assert(critical_depth > 0);
    assert(irq == (critical_depth > 1 ? 0xa5000001u : 0xa5000000u));
    --critical_depth;
    LeaveCriticalSection(&critical);
}
uint32_t tal_system_get_millisecond(void) { return GetTickCount(); }
void tal_system_sleep(uint32_t ms) { assert(!critical_depth); Sleep(ms); }
typedef struct { void (*run)(void *); void *arg; } thread_arg_t;
static DWORD WINAPI thread_run(void *arg)
{ thread_arg_t t=*(thread_arg_t *)arg; free(arg); t.run(t.arg); return 0; }
OPERATE_RET tal_thread_create_and_start(THREAD_HANDLE *h, void (*enter)(void), void (*exit_cb)(void),
    void (*run)(void *), const void *arg, const THREAD_CFG_T *cfg)
{
    (void)enter; (void)exit_cb; (void)cfg;
    thread_arg_t *t=malloc(sizeof(*t)); assert(t); t->run=run; t->arg=(void *)arg;
    *h=CreateThread(NULL,0,thread_run,t,0,NULL); return *h ? 0 : -1;
}
OPERATE_RET tal_thread_delete(THREAD_HANDLE h) { CloseHandle(h); return 0; }
int cli_register_command(const void *c) { (void)c; return 0; }
OPERATE_RET app_breathon_audio_cli_register(void) { return 0; }
void app_breathon_audio_export(const audio_capture_t *c) { (void)c; }
void app_breathon_audio_publish(const breathon_audio_status_t *s) { (void)s; }
void ai_user_event_notify(int t, void *data) { assert(!critical_depth); (void)t; (void)data; }
OPERATE_RET ai_audio_input_start(void) { assert(!critical_depth); return 0; }
OPERATE_RET ai_audio_input_wakeup_set(bool b) { assert(!critical_depth); (void)b; return 0; }
OPERATE_RET tuya_ai_agent_event(int e,int n) { assert(!critical_depth); (void)e; (void)n; return 0; }
void tkl_kws_enable(void) { assert(!critical_depth); InterlockedExchange(&kws,1); }
void tkl_kws_disable(void) { assert(!critical_depth); InterlockedExchange(&kws,0); }
OPERATE_RET ai_audio_player_alert(int n)
{ assert(!critical_depth); (void)n; InterlockedExchange(&playing,1); InterlockedIncrement(&tones); return 0; }
bool ai_audio_player_is_playing(void) { return metric(&playing)!=0; }
OPERATE_RET ai_audio_player_stop(int type)
{
    assert(!critical_depth);
    (void)type;
    EnterCriticalSection(&player_lock); InterlockedExchange(&playing,0); InterlockedIncrement(&stops);
    LeaveCriticalSection(&player_lock);
    SetEvent(feed_release); return 0;
}
OPERATE_RET ai_audio_play_tts_stream(AI_AUDIO_PLAYER_TTS_STATE_E state, AI_AUDIO_CODEC_E codec, char *data,int n)
{
    assert(!critical_depth);
    assert(codec==AI_AUDIO_CODEC_WAV);
    if(state==AI_AUDIO_PLAYER_TTS_START) {
        InterlockedExchange(&playing,1); InterlockedIncrement(&starts); return 0;
    }
    if(state==AI_AUDIO_PLAYER_TTS_STOP) {
        InterlockedIncrement(&eof_count); InterlockedExchange(&playing,0); return 0;
    }
    if(state==AI_AUDIO_PLAYER_TTS_DATA) {
        if(InterlockedExchange(&hold_feed,0)) {
            ResetEvent(feed_release); SetEvent(feed_entered);
            assert(WaitForSingleObject(feed_release,3000)==WAIT_OBJECT_0);
        }
        EnterCriticalSection(&player_lock);
        if(metric(&playing)) {
            assert(n==640 || n==684);
            if(n==684) { assert(!memcmp(data,"RIFF",4)); assert((uint8_t)data[24]==0x80 && (uint8_t)data[25]==0x3e); }
            InterlockedIncrement(&feeds);
        }
        LeaveCriticalSection(&player_lock);
    }
    return 0;
}
void app_breathon_realtime_publish(const char *state,const char *text)
{
    assert(!critical_depth);
    assert(strlen(text)<=512);
    if(!strcmp(state,"listening")) InterlockedIncrement(&listening);
    if(!strcmp(state,"configured")) InterlockedIncrement(&configured);
    if(!strcmp(state,"error")) InterlockedIncrement(&errors);
}
OPERATE_RET netmgr_conn_get(int a,int b,netmgr_status_e *s)
{ (void)a;(void)b;*s=metric(&wifi_up)?NETMGR_LINK_UP:NETMGR_LINK_DOWN;return 0; }
int tal_kv_set(const char *key,const uint8_t *value,size_t n)
{
    assert(!critical_depth && !strcmp(key,"breathon_rt_v1") && n==151);
    if(metric(&check_config_gate)) {
        assert(__atomic_load_n(&rt_request,__ATOMIC_ACQUIRE)==-1);
        assert(!app_breathon_realtime_active());
        app_breathon_realtime_start(); app_breathon_realtime_stop();
        assert(__atomic_load_n(&rt_request,__ATOMIC_ACQUIRE)==-1 && !rt_stop_requested());
        assert(app_breathon_realtime_config("127.0.0.1",1234,"synthetic-lan-token")==OPRT_COM_ERROR);
    }
    if(metric(&kv_fail)) return OPRT_COM_ERROR;
    memcpy(kv,value,n);kv_size=n;return 0;
}
int tal_kv_get(const char *key,uint8_t **value,size_t *n)
{ (void)key;if(!kv_size)return -1;*n=kv_size;*value=malloc(*n);memcpy(*value,kv,*n);return 0; }
int tal_kv_free(uint8_t *value) { free(value);return 0; }
int tal_net_get_errno(void)
{ int n=WSAGetLastError();return n==WSAEWOULDBLOCK?UNW_EWOULDBLOCK:n==WSAEINTR?UNW_EINTR:-99; }
int tal_net_socket_create(int type) { (void)type;return (int)socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); }
int tal_net_disable_nagle(int fd) { int flag=1; return setsockopt((SOCKET)fd,IPPROTO_TCP,TCP_NODELAY,(const char *)&flag,sizeof(flag)); }
int tal_net_set_block(int fd,bool block)
{ u_long n=!block;return ioctlsocket((SOCKET)fd,FIONBIO,&n); }
int tal_net_set_timeout(int fd,int ms,int type)
{ return setsockopt((SOCKET)fd,SOL_SOCKET,type==TRANS_RECV?SO_RCVTIMEO:SO_SNDTIMEO,(const char *)&ms,sizeof(ms)); }
int tal_net_select(int fd,TUYA_FD_SET_T *r,TUYA_FD_SET_T *w,TUYA_FD_SET_T *e,uint32_t ms)
{ struct timeval tv={(long)(ms/1000),(long)(ms%1000)*1000};return select(fd,r,w,e,&tv); }
TUYA_IP_ADDR_T tal_net_str2addr(const char *s) { return ntohl(inet_addr(s)); }
int tal_net_connect(int fd,TUYA_IP_ADDR_T ip,uint16_t port)
{ struct sockaddr_in addr={0};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(ip);addr.sin_port=htons(port);
  InterlockedIncrement(&connections);
  return connect((SOCKET)fd,(struct sockaddr *)&addr,sizeof(addr)); }
int tal_net_send(int fd,const void *data,uint32_t n)
{ if(n>23)n=23;return send((SOCKET)fd,data,(int)n,0); } /* exercise partial sends */
int tal_net_recv(int fd,void *data,uint32_t n) { return recv((SOCKET)fd,data,(int)n,0); }
int tal_net_close(int fd) { return closesocket((SOCKET)fd); }

static void exact(SOCKET s,void *out,size_t n)
{
    uint8_t *p=out;
    while(n) { int got=recv(s,(char *)p,(int)n,0); assert(got>0);p+=got;n-=(size_t)got; }
}
static rt_header_t receive_frame(SOCKET s,uint8_t *out)
{
    uint8_t header[12];rt_header_t h;exact(s,header,12);assert(rt_header_decode(&h,header));
    exact(s,out,h.length);out[h.length]=0;return h;
}
static void send_frame(SOCKET s,uint8_t kind,uint32_t gen,const void *payload,size_t n)
{
    uint8_t frame[4108];assert(rt_header_encode(frame,kind,(uint32_t)n,gen));memcpy(frame+12,payload,n);
    for(size_t pos=0;pos<n+12;) {
        size_t chunk=pos<12?1:97;if(chunk>n+12-pos)chunk=n+12-pos;
        int sent=send(s,(const char *)frame+pos,(int)chunk,0);assert(sent>0);pos+=(size_t)sent;
    }
}
static void control_frame(SOCKET s,uint32_t gen,const char *json) { send_frame(s,RT_CONTROL,gen,json,strlen(json)); }
static SOCKET accept_session(SOCKET listener)
{
    LONG target=metric(&listening)+1;
    fd_set fds;FD_ZERO(&fds);FD_SET(listener,&fds);struct timeval tv={3,0};assert(select(0,&fds,NULL,NULL,&tv)==1);
    SOCKET s=accept(listener,NULL,NULL);assert(s!=INVALID_SOCKET);
    int timeout=3000,one=1;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,(const char *)&timeout,sizeof(timeout));
    setsockopt(s,IPPROTO_TCP,TCP_NODELAY,(const char *)&one,sizeof(one));
    uint8_t body[4097];rt_header_t h=receive_frame(s,body);assert(h.kind==RT_CONTROL && h.generation==0);
    cJSON *j=cJSON_Parse((char *)body);assert(j);assert(!strcmp(cJSON_GetObjectItem(j,"type")->valuestring,"hello"));
    assert(!strcmp(cJSON_GetObjectItem(j,"token")->valuestring,"synthetic-lan-token"));cJSON_Delete(j);
    control_frame(s,0,"{\"type\":\"ready\"}");await_metric(&listening,target);
    assert(app_breathon_audio_owner()==BREATHON_OWNER_REALTIME);return s;
}
static SOCKET open_session(SOCKET listener)
{
    app_breathon_realtime_start();assert(app_breathon_realtime_active());
    return accept_session(listener);
}
#include "test_realtime_sync_cases.h"
int main(int argc,char **argv)
{
    WSADATA wsa;assert(WSAStartup(MAKEWORD(2,2),&wsa)==0);
    InitializeCriticalSection(&critical);InitializeCriticalSection(&player_lock);
    feed_entered=CreateEvent(NULL,TRUE,FALSE,NULL);feed_release=CreateEvent(NULL,TRUE,FALSE,NULL);
    SOCKET listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);assert(listener!=INVALID_SOCKET);
    struct sockaddr_in addr={0};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(listener,(struct sockaddr *)&addr,sizeof(addr))==0);assert(listen(listener,4)==0);
    int addr_size=sizeof(addr);assert(getsockname(listener,(struct sockaddr *)&addr,&addr_size)==0);
    if(argc==2 && !strcmp(argv[1],"--boot")) {
        memcpy(kv,"BRC\1",4);kv[4]=(uint8_t)(ntohs(addr.sin_port)>>8);kv[5]=(uint8_t)ntohs(addr.sin_port);
        strcpy((char *)kv+6,"127.0.0.1");strcpy((char *)kv+22,"synthetic-lan-token");kv_size=151;
    }
    assert(app_breathon_realtime_init()==0 && !app_breathon_realtime_active());
    if(argc==2 && !strcmp(argv[1],"--sync")) {
        test_sync_contract(listener,ntohs(addr.sin_port));
        return 0;
    }
    if(kv_size) {
        await_metric(&configured,1);
        assert(app_breathon_realtime_configured() && !metric(&kws) && ai_audio_local_capture_active());
        fd_set fds;FD_ZERO(&fds);FD_SET(listener,&fds);struct timeval tv={0,100000};
        assert(select(0,&fds,NULL,NULL,&tv)==0);
        puts("persisted KV boot: configured status published, Tuya/KWS gated, no TCP/session start");return 0;
    }
    app_breathon_realtime_start();await_metric(&errors,1);await_idle();
    assert(app_breathon_realtime_config("127.0.0.1",ntohs(addr.sin_port),"synthetic-lan-token")==0);
    assert(kv_size==151 && app_breathon_realtime_configured() && ai_audio_local_capture_active());
    assert(app_breathon_audio_owner()==BREATHON_OWNER_NONE && !metric(&kws));
    InterlockedExchange(&wifi_up,0);app_breathon_realtime_start();await_metric(&errors,2);await_idle();
    InterlockedExchange(&wifi_up,1);
    SOCKET peer=open_session(listener);uint8_t pcm[4096];memset(pcm,0x26,sizeof(pcm));
    AI_NOTIFY_MIC_DATA_T mic={pcm,640};AI_NOTIFY_EVENT_T event={AI_USER_EVT_MIC_DATA,&mic};
    app_breathon_realtime_event(&event);uint8_t body[4097];rt_header_t h=receive_frame(peer,body);
    assert(h.kind==RT_MIC && h.length==640 && !memcmp(body,pcm,640));
    control_frame(peer,1,"{\"type\":\"audio_start\"}");send_frame(peer,RT_SPEAKER,1,pcm,640);
    await_metric(&feeds,1);control_frame(peer,1,"{\"type\":\"audio_end\"}");await_metric(&eof_count,1);
    InterlockedExchange(&hold_feed,1);ResetEvent(feed_entered);
    control_frame(peer,2,"{\"type\":\"audio_start\"}");send_frame(peer,RT_SPEAKER,2,pcm,640);
    assert(WaitForSingleObject(feed_entered,3000)==WAIT_OBJECT_0);
    LONG stop_target=metric(&stops)+1;control_frame(peer,2,"{\"type\":\"interrupt\"}");await_metric(&stops,stop_target);
    send_frame(peer,RT_SPEAKER,2,pcm,640);control_frame(peer,2,"{\"type\":\"audio_start\"}");
    control_frame(peer,3,"{\"type\":\"audio_start\"}");send_frame(peer,RT_SPEAKER,3,pcm,640);await_metric(&feeds,2);
    assert(metric(&starts)==3); /* same-generation restart stayed invalid */
    app_breathon_realtime_stop();await_idle();closesocket(peer);
    assert(!metric(&playing) && !metric(&kws) && ai_audio_local_capture_active());
    /* Other owner must not be able to stop/claim the Realtime player. */
    peer=open_session(listener);assert(!app_breathon_audio_acquire(BREATHON_OWNER_LOCAL));
    stop_target=metric(&stops);app_breathon_audio_release(BREATHON_OWNER_LOCAL);assert(metric(&stops)==stop_target);
    closesocket(peer);await_idle();assert(metric(&errors)==3);
    /* Fill the actual capture budget atomically; the next frame must close. */
    peer=open_session(listener);mic.data_len=RT_FRAME_BYTES;
    uint32_t irq=tal_system_enter_critical();
    for(size_t i=0;i<RT_MIC_CAPACITY/RT_FRAME_BYTES;++i)app_breathon_realtime_event(&event);
    assert(!rt_current->mic_overflow && rt_current->capture_on && rt_current->mic.used==RT_MIC_CAPACITY);
    app_breathon_realtime_event(&event);
    assert(rt_current->mic_overflow && !rt_current->capture_on && rt_current->mic.used==RT_MIC_CAPACITY);
    tal_system_exit_critical(irq);
    await_idle();closesocket(peer);assert(metric(&errors)==4);
    /* Frame advertised length above 4096 must close before allocation. */
    peer=open_session(listener);uint8_t bad[12];assert(rt_header_encode(bad,RT_SPEAKER,4096,1));bad[7]=1;
    assert(send(peer,(const char *)bad,12,0)==12);await_idle();closesocket(peer);assert(metric(&errors)==5);
    /* Block playback while receiving >10KiB. Controls/overflow still run. */
    peer=open_session(listener);ResetEvent(feed_entered);InterlockedExchange(&hold_feed,1);
    control_frame(peer,1,"{\"type\":\"audio_start\"}");send_frame(peer,RT_SPEAKER,1,pcm,640);
    assert(WaitForSingleObject(feed_entered,3000)==WAIT_OBJECT_0);
    for(unsigned i=0;i<3;++i)send_frame(peer,RT_SPEAKER,1,pcm,sizeof(pcm));
    await_idle();closesocket(peer);assert(metric(&errors)==6);
    fd_set fds;FD_ZERO(&fds);FD_SET(listener,&fds);struct timeval tv={0,100000};
    assert(select(0,&fds,NULL,NULL,&tv)==0); /* failures never reconnect */
    printf("localhost TCP workers passed: partial sends/receives, explicit start, KV/provider, 16k MIC, EOF, blocked-feed interrupt, stale generations, owner return, EOF linkloss, overflow, no reconnect; session=%zu bytes\n",sizeof(rt_session_t));
    return 0;
}
