#ifndef BREATHON_AUDIO_CAPTURE_H
#define BREATHON_AUDIO_CAPTURE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define AUDIO_RATE 16000u
#define AUDIO_SAMPLES (AUDIO_RATE * 5u)
#define AUDIO_PCM_BYTES (AUDIO_SAMPLES * 2u)
#define AUDIO_WAV_HEADER 44u

typedef struct {
    uint8_t *wav;
    uint32_t bytes;
    uint32_t clipped;
    uint32_t peak;
    uint32_t frame_mean;
    uint64_t energy;
} audio_capture_t;

void audio_capture_reset(audio_capture_t *capture, uint8_t *wav);
bool audio_capture_append(audio_capture_t *capture, const uint8_t *pcm, size_t bytes);
void audio_capture_finish(audio_capture_t *capture);
uint32_t audio_capture_checksum(const uint8_t *data, size_t bytes);
#endif
