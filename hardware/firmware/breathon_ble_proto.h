#ifndef BREATHON_BLE_PROTO_H
#define BREATHON_BLE_PROTO_H
/*
 * BREATHON BLE protocol v1 (board <-> app). Pure C, no SDK dependency, host-testable.
 *
 * GATT: TuyaOpen common service V2, 16-bit 0xFD50.
 *   write  char 00000001-0000-1001-8001-00805f9b07d0  (app -> board commands)
 *   notify char 00000002-0000-1001-8001-00805f9b07d0  (board -> app frames)
 * Advertised name: "BREATHON".
 *
 * Every frame starts with MAGIC. All frames fit the default 20-byte ATT payload.
 *   command (app -> board): [MAGIC][cmd]
 *   READING (board -> app): [MAGIC][0x01][seq][ppm_x100 lo][ppm_x100 hi][battery][state][flags][progress]
 *   ACK     (board -> app): [MAGIC][0x02][seq][cmd][result]
 * battery 0xFF = unknown (no fuel gauge yet). flags bit0 = reserved (sent as 0), bit1 = final reading.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BBLE_MAGIC 0xB7
#define BBLE_FRAME_READING 0x01
#define BBLE_FRAME_ACK 0x02
#define BBLE_READING_LEN 9
#define BBLE_ACK_LEN 5

#define BBLE_CMD_START 0x10
#define BBLE_CMD_STOP 0x11
#define BBLE_CMD_CALIBRATE 0x12
#define BBLE_CMD_STATUS 0x13

#define BBLE_RES_OK 0
#define BBLE_RES_BUSY 1
#define BBLE_RES_BAD 2

#define BBLE_FLAG_MOCK 0x01
#define BBLE_FLAG_FINAL 0x02
#define BBLE_BATTERY_UNKNOWN 0xFF

/* One tick = 250 ms. */
#define BBLE_TICK_MS 250
#define BBLE_SAMPLE_TICKS 32   /* 8 s exhale window */
#define BBLE_CALIB_TICKS 12    /* 3 s */
#define BBLE_IDLE_BEAT_TICKS 8 /* status heartbeat every 2 s */

typedef enum { BBLE_IDLE = 0, BBLE_SAMPLING = 1, BBLE_CALIBRATING = 2, BBLE_DONE = 3 } bble_state_t;

typedef struct {
    uint8_t state;
    uint8_t seq;
    uint8_t battery;
    uint16_t ticks;     /* ticks spent in current state */
    uint16_t ppm_x100;  /* last reported value */
    uint16_t target_x100;
    bool mock;
    bool ack_pending;
    bool status_pending;
    uint8_t ack_cmd;
    uint8_t ack_res;
} bble_dev_t;

void bble_init(bble_dev_t *dev, uint8_t battery);
/* Parse one write. target_x100 is the value a mock sampling run converges to.
 * Always queues an ACK (bad frames get BBLE_RES_BAD). Returns the result code. */
uint8_t bble_handle_write(bble_dev_t *dev, const uint8_t *data, size_t len, uint16_t target_x100);
/* Advance one tick. Writes at most one frame into out; returns its length (0 = nothing to send). */
size_t bble_tick(bble_dev_t *dev, uint8_t *out, size_t cap);
const char *bble_state_name(uint8_t state);
#endif
