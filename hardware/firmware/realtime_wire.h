#ifndef REALTIME_WIRE_H
#define REALTIME_WIRE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RT_HEADER_SIZE 12u
#define RT_PAYLOAD_MAX 4096u
#define RT_FRAME_BYTES 640u
#define RT_WAV_HEADER_SIZE 44u
enum { RT_CONTROL = 1, RT_MIC = 2, RT_SPEAKER = 3 };
typedef struct { uint32_t length, generation; uint8_t kind; } rt_header_t;
typedef struct {
    uint8_t header[RT_HEADER_SIZE], payload[RT_PAYLOAD_MAX + 1];
    size_t header_used, payload_used;
    rt_header_t frame;
    bool failed;
} rt_parser_t;
typedef bool (*rt_frame_cb)(void *, const rt_header_t *, const uint8_t *);
bool rt_header_encode(uint8_t out[RT_HEADER_SIZE], uint8_t kind, uint32_t length, uint32_t generation);
bool rt_header_decode(rt_header_t *out, const uint8_t data[RT_HEADER_SIZE]);
bool rt_parser_feed(rt_parser_t *parser, const uint8_t *data, size_t length, rt_frame_cb cb, void *arg);

/* Caller supplies storage and synchronization. Writes are all-or-nothing. */
typedef struct { uint8_t *data; size_t capacity, read, used; } rt_ring_t;
void rt_ring_init(rt_ring_t *ring, uint8_t *storage, size_t capacity);
bool rt_ring_write(rt_ring_t *ring, const uint8_t *data, size_t length);
size_t rt_ring_read(rt_ring_t *ring, uint8_t *data, size_t length);
void rt_ring_clear(rt_ring_t *ring);

typedef struct { uint32_t latest; bool open, ended; } rt_generation_t;
bool rt_generation_start(rt_generation_t *state, uint32_t generation);
bool rt_generation_accept(const rt_generation_t *state, uint32_t generation);
bool rt_generation_end(rt_generation_t *state, uint32_t generation);
bool rt_generation_interrupt(rt_generation_t *state, uint32_t generation);
/* Streaming sentinel lengths, not a sample count or playback progress. */
void rt_wav_header(uint8_t out[RT_WAV_HEADER_SIZE]);
bool rt_ipv4_valid(const char *value);
bool rt_utf8_valid(const uint8_t *text, size_t length);
size_t rt_utf8_prefix(const char *text, size_t capacity);
#endif
