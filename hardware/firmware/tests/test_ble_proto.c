/* gcc -std=c11 -Wall -Wextra -Werror -Ifirmware firmware/tests/test_ble_proto.c firmware/breathon_ble_proto.c */
#include "breathon_ble_proto.h"
#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                                         \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static size_t tick(bble_dev_t *d, uint8_t *f) { return bble_tick(d, f, 20); }

static void test_bad_frames(void)
{
    bble_dev_t d;
    uint8_t f[20];
    bble_init(&d, BBLE_BATTERY_UNKNOWN);
    const uint8_t wrong_magic[] = {0x00, BBLE_CMD_START};
    const uint8_t too_long[] = {BBLE_MAGIC, BBLE_CMD_START, 0};
    const uint8_t unknown[] = {BBLE_MAGIC, 0x7F};
    CHECK(bble_handle_write(&d, wrong_magic, 2, 100) == BBLE_RES_BAD);
    CHECK(bble_handle_write(&d, too_long, 3, 100) == BBLE_RES_BAD);
    CHECK(bble_handle_write(&d, NULL, 0, 100) == BBLE_RES_BAD);
    CHECK(bble_handle_write(&d, unknown, 2, 100) == BBLE_RES_BAD);
    CHECK(d.state == BBLE_IDLE);
    CHECK(tick(&d, f) == BBLE_ACK_LEN && f[0] == BBLE_MAGIC && f[1] == BBLE_FRAME_ACK && f[4] == BBLE_RES_BAD);
}

static void test_sampling_run(void)
{
    bble_dev_t d;
    uint8_t f[20];
    bble_init(&d, BBLE_BATTERY_UNKNOWN);
    const uint8_t start[] = {BBLE_MAGIC, BBLE_CMD_START};
    CHECK(bble_handle_write(&d, start, 2, 123) == BBLE_RES_OK);
    CHECK(d.state == BBLE_SAMPLING);
    CHECK(tick(&d, f) == BBLE_ACK_LEN && f[3] == BBLE_CMD_START && f[4] == BBLE_RES_OK);

    uint16_t last = 0;
    int readings = 0, finals = 0;
    uint16_t final_value = 0;
    for (int i = 0; i < BBLE_SAMPLE_TICKS + 4; i++) {
        size_t n = tick(&d, f);
        if (n == 0) continue;
        CHECK(n == BBLE_READING_LEN && f[1] == BBLE_FRAME_READING);
        uint16_t v = (uint16_t)(f[3] | (f[4] << 8));
        CHECK(v >= last);
        CHECK(!(f[7] & BBLE_FLAG_MOCK));
        CHECK(f[5] == BBLE_BATTERY_UNKNOWN);
        last = v;
        readings++;
        if (f[7] & BBLE_FLAG_FINAL) {
            finals++;
            final_value = v;
            CHECK(f[6] == BBLE_DONE && f[8] == 100);
        }
    }
    CHECK(readings >= BBLE_SAMPLE_TICKS / 2 - 1);
    CHECK(finals == 1);
    CHECK(final_value == 123);
    CHECK(d.state == BBLE_DONE);
}

static void test_busy_and_stop(void)
{
    bble_dev_t d;
    uint8_t f[20];
    bble_init(&d, 80);
    const uint8_t calib[] = {BBLE_MAGIC, BBLE_CMD_CALIBRATE};
    const uint8_t start[] = {BBLE_MAGIC, BBLE_CMD_START};
    const uint8_t stop[] = {BBLE_MAGIC, BBLE_CMD_STOP};
    CHECK(bble_handle_write(&d, calib, 2, 0) == BBLE_RES_OK);
    CHECK(bble_handle_write(&d, start, 2, 100) == BBLE_RES_BUSY);
    CHECK(d.state == BBLE_CALIBRATING);
    for (int i = 0; i < BBLE_CALIB_TICKS + 1; i++) tick(&d, f);
    CHECK(d.state == BBLE_IDLE);

    CHECK(bble_handle_write(&d, start, 2, 100) == BBLE_RES_OK);
    CHECK(bble_handle_write(&d, calib, 2, 0) == BBLE_RES_BUSY);
    CHECK(bble_handle_write(&d, stop, 2, 0) == BBLE_RES_OK);
    CHECK(d.state == BBLE_IDLE);
}

static void test_ack_does_not_swallow_final(void)
{
    bble_dev_t d;
    uint8_t f[20];
    bble_init(&d, BBLE_BATTERY_UNKNOWN);
    const uint8_t start[] = {BBLE_MAGIC, BBLE_CMD_START};
    const uint8_t status[] = {BBLE_MAGIC, BBLE_CMD_STATUS};
    bble_handle_write(&d, start, 2, 90);
    for (int i = 0; i < BBLE_SAMPLE_TICKS; i++) tick(&d, f); /* ACK + readings, stops one tick short */
    bble_handle_write(&d, status, 2, 0);                  /* ACK collides with the finishing tick */
    CHECK(tick(&d, f) == BBLE_ACK_LEN);
    CHECK(d.state == BBLE_DONE);
    size_t n = tick(&d, f);
    CHECK(n == BBLE_READING_LEN && (f[7] & BBLE_FLAG_FINAL) && f[3] == 90);
}

static void test_idle_heartbeat_and_seq(void)
{
    bble_dev_t d;
    uint8_t f[20];
    bble_init(&d, BBLE_BATTERY_UNKNOWN);
    int frames = 0;
    uint8_t prev_seq = 0xFF;
    for (int i = 0; i < BBLE_IDLE_BEAT_TICKS * 3; i++) {
        if (tick(&d, f)) {
            CHECK(f[6] == BBLE_IDLE);
            CHECK((uint8_t)(prev_seq + 1) == f[2]);
            prev_seq = f[2];
            frames++;
        }
    }
    CHECK(frames == 3);
    CHECK(bble_tick(&d, f, 4) == 0 || d.ticks % BBLE_IDLE_BEAT_TICKS != 0);
}

int main(void)
{
    test_bad_frames();
    test_sampling_run();
    test_busy_and_stop();
    test_ack_does_not_swallow_final();
    test_idle_heartbeat_and_seq();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("ble proto: all tests passed\n");
    return 0;
}
