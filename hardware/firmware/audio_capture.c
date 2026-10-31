#include "audio_capture.h"
#include <string.h>

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value)
{
    put16(p, (uint16_t)value);
    put16(p + 2, (uint16_t)(value >> 16));
}

void audio_capture_reset(audio_capture_t *capture, uint8_t *wav)
{
    memset(capture, 0, sizeof(*capture));
    capture->wav = wav;
}

bool audio_capture_append(audio_capture_t *capture, const uint8_t *pcm, size_t bytes)
{
    if (bytes % 2 != 0) return false;
    size_t remaining = AUDIO_PCM_BYTES - capture->bytes;
    if (bytes > remaining) bytes = remaining;
    uint32_t sum = 0;
    for (size_t i = 0; i < bytes; i += 2) {
        uint16_t bits = (uint16_t)pcm[i] | ((uint16_t)pcm[i + 1] << 8);
        int32_t sample = bits >= 32768 ? (int32_t)bits - 65536 : bits;
        uint32_t amplitude = sample < 0 ? (uint32_t)-sample : (uint32_t)sample;
        if (amplitude > capture->peak) capture->peak = amplitude;
        if (amplitude >= 32760) capture->clipped++;
        capture->energy += (uint64_t)amplitude * amplitude;
        sum += amplitude;
    }
    if (bytes != 0) {
        capture->frame_mean = sum / (uint32_t)(bytes / 2);
        memcpy(capture->wav + AUDIO_WAV_HEADER + capture->bytes, pcm, bytes);
        capture->bytes += (uint32_t)bytes;
    }
    return true;
}

void audio_capture_finish(audio_capture_t *capture)
{
    uint8_t *h = capture->wav;
    memset(h, 0, AUDIO_WAV_HEADER);
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36 + capture->bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    put16(h + 20, 1);
    put16(h + 22, 1);
    put32(h + 24, AUDIO_RATE);
    put32(h + 28, AUDIO_RATE * 2);
    put16(h + 32, 2);
    put16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    put32(h + 40, capture->bytes);
}

uint32_t audio_capture_checksum(const uint8_t *data, size_t bytes)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < bytes; ++i) hash = (hash ^ data[i]) * 16777619u;
    return hash;
}
