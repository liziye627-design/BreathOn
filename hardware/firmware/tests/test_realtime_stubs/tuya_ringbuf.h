#include "tal_api.h"
#include "realtime_wire.h"
typedef struct { rt_ring_t ring; uint8_t data[16384]; } *TUYA_RINGBUFF_T;
enum { OVERFLOW_STOP_TYPE };
static inline int tuya_ring_buff_create(unsigned n, int type, TUYA_RINGBUFF_T *out)
{ (void)type; *out = malloc(sizeof(**out)); if (!*out) return OPRT_MALLOC_FAILED; rt_ring_init(&(*out)->ring, (*out)->data, n); return 0; }
static inline void tuya_ring_buff_reset(TUYA_RINGBUFF_T r) { rt_ring_clear(&r->ring); }
static inline void tuya_ring_buff_free(TUYA_RINGBUFF_T r) { free(r); }
static inline int tuya_ring_buff_write(TUYA_RINGBUFF_T r, const void *p, unsigned n)
{ unsigned room = (unsigned)(r->ring.capacity - r->ring.used); if (n > room) n = room; return rt_ring_write(&r->ring, p, n) ? (int)n : 0; }
static inline int tuya_ring_buff_read(TUYA_RINGBUFF_T r, void *p, unsigned n) { return (int)rt_ring_read(&r->ring, p, n); }
