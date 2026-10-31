#include "realtime_wire.h"
#include <string.h>

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static void put32(uint8_t *p, uint32_t n)
{
    p[0] = (uint8_t)(n >> 24); p[1] = (uint8_t)(n >> 16);
    p[2] = (uint8_t)(n >> 8); p[3] = (uint8_t)n;
}
static bool valid(uint8_t kind, uint32_t length)
{
    return kind >= RT_CONTROL && kind <= RT_SPEAKER && length > 0 &&
        length <= RT_PAYLOAD_MAX && (kind == RT_CONTROL || !(length & 1));
}
bool rt_header_encode(uint8_t out[RT_HEADER_SIZE], uint8_t kind, uint32_t length, uint32_t generation)
{
    if (!valid(kind, length)) return false;
    out[0] = 'B'; out[1] = 'R'; out[2] = 1; out[3] = kind;
    put32(out + 4, length); put32(out + 8, generation);
    return true;
}
bool rt_header_decode(rt_header_t *out, const uint8_t p[RT_HEADER_SIZE])
{
    if (p[0] != 'B' || p[1] != 'R' || p[2] != 1 || !valid(p[3], be32(p + 4))) return false;
    out->kind = p[3]; out->length = be32(p + 4); out->generation = be32(p + 8);
    return true;
}
bool rt_parser_feed(rt_parser_t *p, const uint8_t *data, size_t length, rt_frame_cb cb, void *arg)
{
    if (p->failed) return false;
    while (length) {
        if (p->header_used < RT_HEADER_SIZE) {
            size_t n = RT_HEADER_SIZE - p->header_used; if (n > length) n = length;
            memcpy(p->header + p->header_used, data, n);
            p->header_used += n; data += n; length -= n;
            if (p->header_used < RT_HEADER_SIZE) continue;
            if (!rt_header_decode(&p->frame, p->header)) { p->failed = true; return false; }
        }
        size_t n = p->frame.length - p->payload_used; if (n > length) n = length;
        memcpy(p->payload + p->payload_used, data, n);
        p->payload_used += n; data += n; length -= n;
        if (p->payload_used == p->frame.length) {
            p->payload[p->payload_used] = 0;
            if (!cb(arg, &p->frame, p->payload)) { p->failed = true; return false; }
            p->header_used = p->payload_used = 0;
        }
    }
    return true;
}
void rt_ring_init(rt_ring_t *r, uint8_t *storage, size_t capacity)
{
    r->data = storage; r->capacity = capacity; r->read = r->used = 0;
}
void rt_ring_clear(rt_ring_t *r) { r->read = r->used = 0; }
bool rt_ring_write(rt_ring_t *r, const uint8_t *data, size_t length)
{
    if (length > r->capacity - r->used) return false;
    if (!length) return true;
    size_t pos = (r->read + r->used) % r->capacity;
    size_t first = r->capacity - pos; if (first > length) first = length;
    memcpy(r->data + pos, data, first); memcpy(r->data, data + first, length - first);
    r->used += length;
    return true;
}
size_t rt_ring_read(rt_ring_t *r, uint8_t *data, size_t length)
{
    if (length > r->used) length = r->used;
    if (!length) return 0;
    size_t first = r->capacity - r->read; if (first > length) first = length;
    memcpy(data, r->data + r->read, first); memcpy(data + first, r->data, length - first);
    r->read = (r->read + length) % r->capacity; r->used -= length;
    return length;
}
bool rt_generation_start(rt_generation_t *g, uint32_t n)
{
    if (!n || n <= g->latest) return false;
    g->latest = n; g->open = true; g->ended = false; return true;
}
bool rt_generation_accept(const rt_generation_t *g, uint32_t n)
{ return g->open && !g->ended && n == g->latest; }
bool rt_generation_end(rt_generation_t *g, uint32_t n)
{
    if (!rt_generation_accept(g, n)) return false;
    g->ended = true; return true;
}
bool rt_generation_interrupt(rt_generation_t *g, uint32_t n)
{
    if (!g->open || n != g->latest) return false;
    g->open = false; return true;
}
void rt_wav_header(uint8_t out[RT_WAV_HEADER_SIZE])
{
    /* The pinned SDK's WAV decoder ignores RIFF/data lengths; STOP supplies EOF.
     * Sentinels avoid wraparound and never claim feed bytes were heard. */
    static const uint8_t header[44] = {
        'R','I','F','F', 0xff,0xff,0xff,0xff, 'W','A','V','E',
        'f','m','t',' ', 16,0,0,0, 1,0, 1,0, 0x80,0x3e,0,0,
        0x00,0x7d,0,0, 2,0, 16,0, 'd','a','t','a', 0xfe,0xff,0xff,0xff
    };
    memcpy(out, header, sizeof(header));
}
bool rt_ipv4_valid(const char *value)
{
    if (!value) return false;
    const char *p = value;
    uint32_t address = 0;
    for (unsigned i = 0; i < 4; ++i) {
        unsigned n = 0, digits = 0; bool leading_zero = *p == '0';
        while (*p >= '0' && *p <= '9') {
            if (++digits > 3) return false;
            n = n * 10 + (unsigned)(*p++ - '0');
        }
        if (!digits || (leading_zero && digits > 1) || n > 255 || (i < 3 ? *p++ != '.' : *p != 0)) return false;
        address = (address << 8) | n;
    }
    return address != 0 && address != UINT32_MAX;
}
bool rt_utf8_valid(const uint8_t *text, size_t length)
{
    for (size_t i = 0; i < length;) {
        uint32_t cp = text[i++], minimum; unsigned more;
        if (cp < 0x80) continue;
        if (cp >= 0xc2 && cp <= 0xdf) { cp &= 31; more = 1; minimum = 0x80; }
        else if (cp >= 0xe0 && cp <= 0xef) { cp &= 15; more = 2; minimum = 0x800; }
        else if (cp >= 0xf0 && cp <= 0xf4) { cp &= 7; more = 3; minimum = 0x10000; }
        else return false;
        if (more > length - i) return false;
        while (more--) {
            uint8_t c = text[i++]; if ((c & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (c & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    return true;
}
size_t rt_utf8_prefix(const char *text, size_t capacity)
{
    size_t n = strlen(text); if (n <= capacity) return n;
    n = capacity;
    while (n && (((unsigned char)text[n] & 0xc0) == 0x80)) --n;
    return n;
}
