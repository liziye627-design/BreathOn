#include "audio_capture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t storage[AUDIO_WAV_HEADER + AUDIO_PCM_BYTES + 8];
static uint8_t input[AUDIO_PCM_BYTES + 10];

int main(void)
{
    audio_capture_t capture;
    memset(storage, 0xa5, sizeof(storage));
    audio_capture_reset(&capture, storage);
    const uint8_t samples[] = {0, 0, 0xff, 0x7f, 0, 0x80, 1, 0};
    assert(!audio_capture_append(&capture, samples, 3));
    assert(capture.bytes == 0);
    assert(audio_capture_append(&capture, samples, sizeof(samples)));
    assert(capture.bytes == 8 && capture.peak == 32768 && capture.clipped == 2);
    assert(capture.energy == 2147418114ULL);
    audio_capture_finish(&capture);
    assert(memcmp(storage, "RIFF", 4) == 0 && memcmp(storage + 8, "WAVEfmt ", 8) == 0);
    assert(storage[22] == 1 && storage[24] == 0x80 && storage[25] == 0x3e);
    assert(storage[34] == 16 && storage[40] == 8);
    assert(memcmp(storage + 44, samples, 8) == 0);
    assert(audio_capture_checksum((const uint8_t *)"hello", 5) == 0x4f9f2cab);
    assert(audio_capture_append(&capture, input, sizeof(input)));
    assert(capture.bytes == AUDIO_PCM_BYTES);
    for (size_t i = AUDIO_WAV_HEADER + AUDIO_PCM_BYTES; i < sizeof(storage); ++i)
        assert(storage[i] == 0xa5);
    audio_capture_finish(&capture);
    assert(storage[40] == 0 && storage[41] == 0x71 && storage[42] == 2);
    assert(audio_capture_append(&capture, samples, sizeof(samples)) && capture.bytes == AUDIO_PCM_BYTES);
    audio_capture_reset(&capture, storage);
    assert(capture.bytes == 0 && capture.energy == 0 && capture.peak == 0);
    puts("PASS: PCM bounds, odd input, signed peaks, clipping, WAV fields, checksum, reset");
    return 0;
}
