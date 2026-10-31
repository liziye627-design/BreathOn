/* Deterministic host test: real playback worker/ring, virtual time, no sockets.
 * From the project root:
 * gcc -std=c11 -Wall -Wextra -Werror -Ifirmware/tests/test_realtime_stubs
 *     -Ifirmware firmware/tests/test_realtime_pacing.c
 *     firmware/app_breathon_realtime_playback.c firmware/realtime_wire.c
 *     -o <temporary-directory>/test_realtime_pacing.exe
 * Run without arguments for all cases, or pass one case name printed below.
 */
#include "app_breathon_realtime_internal.h"
#include <setjmp.h>

typedef struct {
    const char *name;
    unsigned frames, overhead_ms, queue_limit;
    uint32_t clock_origin;
    bool jitter, gap;
} scenario_t;

rt_session_t *rt_current;
bool rt_playback_idle;
int rt_request;
static rt_session_t session;
static scenario_t scenario;
static jmp_buf finished;
static unsigned elapsed, produced, feeds, starts, ends, critical_depth;
static size_t submitted, consumed, peak_queue;
static uint32_t recent_feeds[3];
static bool playing;

static void require(bool condition, const char *message)
{
    if (condition) return;
    fprintf(stderr, "FAIL %s: %s at %ums; arrivals=%u feeds=%u queue=%zu peak=%zu\n",
            scenario.name, message, elapsed, produced, feeds, session.speaker.used, peak_queue);
    longjmp(finished, 2);
}

static unsigned arrival_time(unsigned index)
{
    if (!scenario.gap || !index) return index * 20;
    /* A delayed producer resumes with 240ms of buffered PCM, within capacity. */
    return index <= 12 ? 1000 : 1000 + (index - 12) * 20;
}

static void advance_time(unsigned ms)
{
    unsigned target = elapsed + ms;
    while (produced <= scenario.frames && arrival_time(produced) <= target) {
        elapsed = arrival_time(produced);
        uint8_t pcm[RT_FRAME_BYTES];
        size_t length = produced == scenario.frames ? 318 : sizeof(pcm);
        for (size_t i = 0; i < length; ++i) pcm[i] = (uint8_t)((submitted + i) % 251);
        require(rt_ring_write(&session.speaker, pcm, length), "speaker queue overflow");
        submitted += length; ++produced;
        if (session.speaker.used > peak_queue) peak_queue = session.speaker.used;
        if (produced > scenario.frames) rt_generation_end(&session.generation, 1);
    }
    elapsed = target;
    require(elapsed <= arrival_time(scenario.frames) + 3000, "playback did not drain");
}

uint32_t tal_system_enter_critical(void) { return critical_depth++; }
void tal_system_exit_critical(uint32_t saved)
{
    require(critical_depth && --critical_depth == saved, "critical section mismatch");
}
uint32_t tal_system_get_millisecond(void) { return scenario.clock_origin + elapsed; }
void tal_system_sleep(uint32_t ms)
{
    require(!critical_depth, "sleep inside critical section");
    require(!session.failure, "worker reported playback failure");
    advance_time(ms);
}
bool rt_stop_requested(void) { return false; }
bool ai_audio_player_is_playing(void) { return playing; }
OPERATE_RET ai_audio_player_stop(int type)
{
    require(!critical_depth && type == AI_AUDIO_PLAYER_ALL, "invalid player stop");
    playing = false;
    return OPRT_OK;
}

OPERATE_RET ai_audio_play_tts_stream(AI_AUDIO_PLAYER_TTS_STATE_E state,
                                    AI_AUDIO_CODEC_E codec, char *data, int length)
{
    require(!critical_depth && codec == AI_AUDIO_CODEC_WAV, "invalid player call");
    if (state == AI_AUDIO_PLAYER_TTS_START) {
        require(!starts++, "unexpected player restart");
        playing = true;
        return OPRT_OK;
    }
    if (state == AI_AUDIO_PLAYER_TTS_STOP) {
        ++ends; playing = false;
        require(consumed == submitted && produced == scenario.frames + 1, "EOF lost PCM");
        longjmp(finished, 1);
    }
    require(state == AI_AUDIO_PLAYER_TTS_DATA && playing, "unexpected player state");
    require(data && length > 0, "empty feed");
    if (!feeds) {
        uint8_t header[RT_WAV_HEADER_SIZE]; rt_wav_header(header);
        require(length > (int)sizeof(header) && !memcmp(data, header, sizeof(header)), "WAV header missing");
        data += sizeof(header); length -= (int)sizeof(header);
    }
    require(length <= (int)RT_FRAME_BYTES && !(length & 1), "invalid PCM length");
    for (int i = 0; i < length; ++i)
        require((uint8_t)data[i] == (uint8_t)((consumed + (size_t)i) % 251), "PCM dropped/reordered");
    /* At most one normal frame plus two catch-up frames in any 20ms window. */
    if (scenario.gap && feeds >= 3)
        require(elapsed - recent_feeds[feeds % 3] >= 20, "catch-up exceeded three frames/20ms");
    recent_feeds[feeds % 3] = elapsed;
    ++feeds; consumed += (size_t)length;
    unsigned overhead = scenario.overhead_ms;
    if (scenario.jitter) {
        overhead += feeds % 3;
        if (feeds % 71 == 0) overhead += 37;
    }
    /* Producer keeps arriving while the SDK occupies the playback worker. */
    advance_time(overhead);
    return OPRT_OK;
}

static int run_scenario(const scenario_t *value)
{
    /* Given: a 16-frame ring, 16k/s16 PCM every 20ms, and SDK feed overhead. */
    scenario = *value;
    memset(&session, 0, sizeof(session)); memset(recent_feeds, 0, sizeof(recent_feeds));
    elapsed = produced = feeds = starts = ends = critical_depth = 0;
    submitted = consumed = peak_queue = 0; playing = false;
    rt_current = &session; rt_playback_idle = false;
    rt_ring_init(&session.speaker, session.speaker_storage, sizeof(session.speaker_storage));
    rt_generation_start(&session.generation, 1);
    switch (setjmp(finished)) {
    case 0:
        advance_time(0);
        /* When: the production worker runs through the full response and EOF. */
        rt_playback_task(NULL);
        return 1;
    case 1:
        break;
    default:
        return 1;
    }
    /* Then: finite queue headroom, exact samples including the 159-sample tail. */
    require(!critical_depth && !session.failure && starts == 1 && ends == 1, "unclean completion");
    require(peak_queue <= scenario.queue_limit * RT_FRAME_BYTES, "queue headroom exhausted");
    require(feeds == scenario.frames + 1 && !session.speaker.used, "PCM not fully drained");
    printf("PASS %s: %u frames + 159 samples, overhead=%ums, elapsed=%ums, peak=%zu/%zu bytes\n",
           scenario.name, scenario.frames, scenario.overhead_ms, elapsed, peak_queue,
           session.speaker.capacity);
    return 0;
}

int main(int argc, char **argv)
{
    const scenario_t cases[] = {
        {"zero-overhead", 9000, 0, 2, 0, false, false},
        {"sustained-5ms", 9000, 5, 2, 0, false, false},
        {"jitter", 9000, 3, 4, 0, true, false},
        {"bounded-catch-up", 100, 1, 13, 0, false, true},
        {"clock-wrap", 9000, 5, 2, UINT32_MAX - 19, false, false},
    };
    unsigned selected = 0; int failed = 0;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (argc > 1 && strcmp(argv[1], cases[i].name)) continue;
        ++selected; failed |= run_scenario(&cases[i]);
    }
    if (!selected || argc > 2) { fputs("Unknown pacing test case\n", stderr); return 2; }
    return failed;
}
