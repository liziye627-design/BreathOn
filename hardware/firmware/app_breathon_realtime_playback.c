#include "app_breathon_realtime_internal.h"

static bool still_current(uint32_t generation)
{
    uint32_t irq = tal_system_enter_critical();
    bool current = rt_current && rt_current->generation.open &&
                   rt_current->generation.latest == generation && !rt_current->failure &&
                   !rt_current->mic_overflow && !rt_stop_requested();
    tal_system_exit_critical(irq);
    return current;
}
static void fail_player(void)
{
    uint32_t irq = tal_system_enter_critical();
    if (rt_current) rt_current->failure = "音频播放失败";
    tal_system_exit_critical(irq);
}
void rt_playback_task(void *arg)
{
    (void)arg;
    uint8_t frame[RT_WAV_HEADER_SIZE + RT_FRAME_BYTES];
    uint32_t playing_generation = 0, next_feed = 0, drain_since = 0;
    uint32_t pace_start = 0, paced_samples = 0;
    bool started = false, eof = false;
    for (;;) {
        uint32_t irq = tal_system_enter_critical();
        rt_session_t *s = rt_current;
        uint32_t generation = s && s->generation.open && !rt_stop_requested() ? s->generation.latest : 0;
        bool was_idle = rt_playback_idle;
        if (s) rt_playback_idle = false;
        tal_system_exit_critical(irq);
        if (generation != playing_generation || (!s && !was_idle)) {
            /* Also stops a stale START/feed that completed after network cancel. */
            if (started) ai_audio_player_stop(AI_AUDIO_PLAYER_ALL);
            started = eof = false; playing_generation = generation; next_feed = 0;
            paced_samples = 0;
        }
        if (!s) {
            irq = tal_system_enter_critical(); rt_playback_idle = true; tal_system_exit_critical(irq);
            tal_system_sleep(5); continue;
        }
        if (!generation) { tal_system_sleep(5); continue; }
        uint32_t now = tal_system_get_millisecond();
        if (paced_samples && (int32_t)(now - next_feed) < 0) { tal_system_sleep(2); continue; }
        irq = tal_system_enter_critical();
        s = rt_current;
        size_t count = 0; bool ended = false;
        if (s && s->generation.open && s->generation.latest == generation) {
            ended = s->generation.ended;
            if (s->speaker.used >= RT_FRAME_BYTES || ended)
                count = rt_ring_read(&s->speaker, frame + RT_WAV_HEADER_SIZE, RT_FRAME_BYTES);
        }
        tal_system_exit_critical(irq);
        if (count && still_current(generation)) {
            bool first = !started;
            if (first) {
                /* Pinned SDK has no PCM enum. WAV decoder reads 16000/s16/mono
                 * here; it ignores sentinel RIFF/data lengths. Always combine
                 * header and first PCM: its data-tag scan rejects a lone 44B. */
                rt_wav_header(frame);
                OPERATE_RET result = ai_audio_play_tts_stream(AI_AUDIO_PLAYER_TTS_START, AI_AUDIO_CODEC_WAV, NULL, 0);
                if (result != OPRT_OK && still_current(generation))
                    fail_player();
                started = true;
            }
            if (still_current(generation)) {
                uint32_t before = tal_system_get_millisecond();
                char *data = (char *)frame + (first ? 0 : RT_WAV_HEADER_SIZE);
                int length = (int)count + (first ? RT_WAV_HEADER_SIZE : 0);
                OPERATE_RET result = ai_audio_play_tts_stream(AI_AUDIO_PLAYER_TTS_DATA, AI_AUDIO_CODEC_WAV, data, length);
                if (still_current(generation) && (result != OPRT_OK ||
                    (uint32_t)(tal_system_get_millisecond() - before) > 100))
                    fail_player(); /* SDK feed timeout may otherwise silently lose PCM. */
                /* Anchor to samples, not SDK completion time. Retain at most
                 * 40ms (two frames) of catch-up debt after a stall/empty queue.
                 * Pacing only: this is NEVER reported as heard/DAC progress. */
                if (!paced_samples) pace_start = before;
                else if ((int32_t)(before - next_feed) > 40)
                    pace_start += (uint32_t)(before - next_feed) - 40;
                paced_samples += (uint32_t)count / 2;
                next_feed = pace_start + (paced_samples + 15) / 16; /* 16 samples/ms */
            }
            if (!still_current(generation)) {
                ai_audio_player_stop(AI_AUDIO_PLAYER_ALL); started = false;
            }
        } else if (!count && ended && started && !eof && still_current(generation)) {
            if (ai_audio_play_tts_stream(AI_AUDIO_PLAYER_TTS_STOP, AI_AUDIO_CODEC_WAV, NULL, 0) != OPRT_OK)
                fail_player();
            eof = true; drain_since = now;
        } else if (eof && (uint32_t)(now - drain_since) > 3000 && ai_audio_player_is_playing()) {
            fail_player();
        }
        tal_system_sleep(2);
    }
}
