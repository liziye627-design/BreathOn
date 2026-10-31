#include "realtime_wire.h"
#include "decoder_cfg.h"
#include "datasink_cfg.h"
#include <assert.h>
extern DECODER_T g_decoder_wav;
extern DATASINK_T g_datasink_mem;
OPERATE_RET tal_mutex_create_init(MUTEX_HANDLE *h) { *h=(void *)1; return 0; }
OPERATE_RET tal_mutex_lock(MUTEX_HANDLE h) { (void)h; return 0; }
OPERATE_RET tal_mutex_unlock(MUTEX_HANDLE h) { (void)h; return 0; }
OPERATE_RET tal_mutex_release(MUTEX_HANDLE h) { (void)h; return 0; }
void tal_system_sleep(uint32_t n) { (void)n; assert(!"unexpected backpressure"); }
int main(void)
{
    void *decoder = NULL, *sink = NULL;
    uint8_t input[684], output[640], readbuf[684];
    rt_wav_header(input);
    for (unsigned i=44;i<sizeof(input);++i) input[i]=(uint8_t)i;
    assert(g_decoder_wav.start(&decoder)==0);
    assert(g_datasink_mem.start(NULL, &sink)==0);
    assert(g_datasink_mem.feed(sink,input,sizeof(input))==0);
    /* Real SDK STOP sends feed(NULL,0); accepted PCM must drain before EOF. */
    assert(g_datasink_mem.feed(sink,NULL,0)==0);
    uint32_t count=0; DECODER_OUTPUT_T fmt={0};
    assert(g_datasink_mem.read(sink,readbuf,sizeof(readbuf),&count)==0 && count==684);
    assert(g_decoder_wav.process(decoder,readbuf,(int)count,output,sizeof(output),&fmt)==0);
    assert(fmt.sample==16000 && fmt.datebits==16 && fmt.channel==1 && fmt.samples==320);
    assert(!memcmp(input+44,output,640));
    /* 200 later frames: sentinel data length must never truncate at first frame. */
    for(unsigned i=0;i<200;++i) {
        assert(g_decoder_wav.process(decoder,input+44,640,output,640,&fmt)==0);
        assert(fmt.samples==320 && !memcmp(input+44,output,640));
    }
    assert(g_datasink_mem.read(sink,readbuf,sizeof(readbuf),&count)==OPRT_NOT_FOUND && count==0);
    assert(g_datasink_mem.start(NULL,&sink)==0);
    assert(g_datasink_mem.feed(sink,input,684)==0);
    assert(g_datasink_mem.stop(sink)==0);
    assert(g_datasink_mem.read(sink,readbuf,sizeof(readbuf),&count)==0 && count==0);
    assert(g_datasink_mem.exit(sink)==0); assert(g_decoder_wav.stop(decoder)==0);
    puts("pinned SDK WAV 16k/16bit/mono, 201 frames, EOF drain and abort reset passed");
    return 0;
}
