#include "realtime_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned frames;
static bool received(void *arg, const rt_header_t *h, const uint8_t *data)
{
    (void)arg;
    assert(h->kind == RT_SPEAKER && h->generation == 0x12345678 && h->length == 640);
    for (size_t i = 0; i < h->length; ++i) assert(data[i] == (uint8_t)i);
    ++frames;
    return true;
}
static void test_fragmented_and_coalesced_frames(void)
{
    uint8_t bytes[(RT_HEADER_SIZE + RT_FRAME_BYTES) * 2];
    assert(rt_header_encode(bytes, RT_SPEAKER, RT_FRAME_BYTES, 0x12345678));
    for (size_t i = 0; i < RT_FRAME_BYTES; ++i) bytes[RT_HEADER_SIZE + i] = (uint8_t)i;
    memcpy(bytes + sizeof(bytes)/2, bytes, sizeof(bytes)/2);
    for (size_t chunk = 1; chunk <= sizeof(bytes); ++chunk) {
        rt_parser_t parser = {0}; frames = 0;
        for (size_t pos = 0; pos < sizeof(bytes); pos += chunk) {
            size_t n = sizeof(bytes) - pos; if (n > chunk) n = chunk;
            assert(rt_parser_feed(&parser, bytes + pos, n, received, NULL));
        }
        assert(frames == 2);
    }
}
static void test_invalid_frames_fail_closed(void)
{
    uint8_t h[12]; rt_header_t parsed;
    assert(!rt_header_encode(h, RT_SPEAKER, 3, 1));
    assert(!rt_header_encode(h, RT_CONTROL, 4097, 0));
    assert(!rt_header_encode(h, RT_CONTROL, 0, 0));
    assert(!rt_header_encode(h, 4, 2, 1));
    assert(rt_header_encode(h, RT_CONTROL, 4096, 0));
    h[4] = 0xff;
    assert(!rt_header_decode(&parsed, h));
    rt_parser_t parser = {0};
    assert(!rt_parser_feed(&parser, h, sizeof(h), received, NULL));
    assert(rt_header_encode(h, RT_CONTROL, 2, 0));
    assert(!rt_parser_feed(&parser, h, sizeof(h), received, NULL));
    h[2] = 2; assert(!rt_header_decode(&parsed, h));
    h[2] = 1; h[0] = 'X'; assert(!rt_header_decode(&parsed, h));
}
static void test_ring_wrap_and_overflow(void)
{
    uint8_t data[8], out[8]; rt_ring_t ring;
    rt_ring_init(&ring, data, sizeof(data));
    assert(rt_ring_write(&ring, (const uint8_t *)"abcdef", 6));
    assert(!rt_ring_write(&ring, (const uint8_t *)"XYZ", 3));
    assert(rt_ring_read(&ring, out, 4) == 4 && !memcmp(out,"abcd",4));
    assert(rt_ring_write(&ring, (const uint8_t *)"ghijkl", 6));
    assert(rt_ring_read(&ring, out, 8) == 8 && !memcmp(out,"efghijkl",8));
    assert(rt_ring_write(&ring, (const uint8_t *)"12345678", 8));
    rt_ring_clear(&ring); assert(rt_ring_read(&ring, out, 8) == 0);
}
static void test_cancel_and_end_never_resurrect(void)
{
    rt_generation_t g = {0};
    assert(!rt_generation_start(&g, 0));
    assert(rt_generation_start(&g, 9));
    assert(rt_generation_accept(&g, 9));
    assert(!rt_generation_interrupt(&g, 8));
    assert(rt_generation_interrupt(&g, 9));
    assert(!rt_generation_accept(&g, 9));
    assert(!rt_generation_start(&g, 9));
    assert(!rt_generation_end(&g, 9));
    assert(rt_generation_start(&g, 10));
    assert(rt_generation_end(&g, 10));
    assert(!rt_generation_accept(&g, 10));
    assert(rt_generation_interrupt(&g, 10)); /* cancel while draining */
    assert(rt_generation_start(&g, UINT32_MAX));
    assert(!rt_generation_start(&g, 1)); /* no generation wrap */
}
int main(void)
{
    test_fragmented_and_coalesced_frames(); test_invalid_frames_fail_closed();
    test_ring_wrap_and_overflow(); test_cancel_and_end_never_resurrect();
    assert(rt_ipv4_valid("192.168.1.9")); assert(!rt_ipv4_valid("999.1.1.1"));
    assert(!rt_ipv4_valid("1.1.1.1x")); assert(!rt_ipv4_valid("1.1.1"));
    assert(!rt_ipv4_valid("0.0.0.0")); assert(!rt_ipv4_valid("255.255.255.255"));
    assert(!rt_ipv4_valid("000.000.000.000")); assert(!rt_ipv4_valid("010.1.1.1"));
    assert(rt_utf8_prefix("a\xe4\xb8\xad", 3) == 1);
    assert(rt_utf8_valid((const uint8_t *)"a\xe4\xb8\xad", 4));
    assert(!rt_utf8_valid((const uint8_t *)"\xc0\x80", 2));
    assert(!rt_utf8_valid((const uint8_t *)"\xed\xa0\x80", 3));
    assert(!rt_utf8_valid((const uint8_t *)"\xf4\x90\x80\x80", 4));
    puts("realtime protocol/ring/generation tests passed");
    return 0;
}
