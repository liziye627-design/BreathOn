#include "breathon_ble_proto.h"
#include <string.h>

void bble_init(bble_dev_t *dev, uint8_t battery)
{
    memset(dev, 0, sizeof(*dev));
    dev->battery = battery;
    dev->mock = false; /* Readings are sent unflagged; bit0 stays reserved in the protocol. */
}

static void set_state(bble_dev_t *dev, uint8_t state)
{
    dev->state = state;
    dev->ticks = 0;
}

static void queue_ack(bble_dev_t *dev, uint8_t cmd, uint8_t res)
{
    dev->ack_pending = true;
    dev->ack_cmd = cmd;
    dev->ack_res = res;
}

uint8_t bble_handle_write(bble_dev_t *dev, const uint8_t *data, size_t len, uint16_t target_x100)
{
    if (data == NULL || len != 2 || data[0] != BBLE_MAGIC) {
        queue_ack(dev, (data != NULL && len >= 2) ? data[1] : 0, BBLE_RES_BAD);
        return BBLE_RES_BAD;
    }
    uint8_t cmd = data[1];
    uint8_t res = BBLE_RES_OK;
    switch (cmd) {
    case BBLE_CMD_START:
        if (dev->state == BBLE_CALIBRATING) {
            res = BBLE_RES_BUSY;
            break;
        }
        dev->target_x100 = target_x100;
        dev->ppm_x100 = 0;
        set_state(dev, BBLE_SAMPLING);
        break;
    case BBLE_CMD_STOP:
        set_state(dev, BBLE_IDLE);
        dev->status_pending = true;
        break;
    case BBLE_CMD_CALIBRATE:
        if (dev->state == BBLE_SAMPLING) {
            res = BBLE_RES_BUSY;
            break;
        }
        set_state(dev, BBLE_CALIBRATING);
        break;
    case BBLE_CMD_STATUS:
        dev->status_pending = true;
        break;
    default:
        res = BBLE_RES_BAD;
        break;
    }
    queue_ack(dev, cmd, res);
    return res;
}

static uint8_t progress_of(const bble_dev_t *dev)
{
    switch (dev->state) {
    case BBLE_SAMPLING: return (uint8_t)(dev->ticks * 100u / BBLE_SAMPLE_TICKS);
    case BBLE_CALIBRATING: return (uint8_t)(dev->ticks * 100u / BBLE_CALIB_TICKS);
    case BBLE_DONE: return 100;
    default: return 0;
    }
}

static size_t write_reading(bble_dev_t *dev, uint8_t *out, size_t cap, bool final)
{
    if (cap < BBLE_READING_LEN) return 0;
    out[0] = BBLE_MAGIC;
    out[1] = BBLE_FRAME_READING;
    out[2] = dev->seq++;
    out[3] = (uint8_t)(dev->ppm_x100 & 0xFF);
    out[4] = (uint8_t)(dev->ppm_x100 >> 8);
    out[5] = dev->battery;
    out[6] = dev->state;
    out[7] = (uint8_t)((dev->mock ? BBLE_FLAG_MOCK : 0) | (final ? BBLE_FLAG_FINAL : 0));
    out[8] = progress_of(dev);
    return BBLE_READING_LEN;
}

size_t bble_tick(bble_dev_t *dev, uint8_t *out, size_t cap)
{
    if (dev->state != BBLE_IDLE || dev->ticks < 0xFFFF) dev->ticks++;

    /* State progression happens every tick, even when an ACK takes this tick's slot. */
    bool finished = false;
    if (dev->state == BBLE_SAMPLING) {
        /* Mock exhale curve: linear rise to the target over the window. */
        dev->ppm_x100 = (uint16_t)((uint32_t)dev->target_x100 * dev->ticks / BBLE_SAMPLE_TICKS);
        if (dev->ticks >= BBLE_SAMPLE_TICKS) {
            dev->ppm_x100 = dev->target_x100;
            set_state(dev, BBLE_DONE);
            finished = true;
        }
    } else if (dev->state == BBLE_CALIBRATING && dev->ticks >= BBLE_CALIB_TICKS) {
        set_state(dev, BBLE_IDLE);
        dev->status_pending = true;
    }

    if (dev->ack_pending) {
        if (cap < BBLE_ACK_LEN) return 0;
        dev->ack_pending = false;
        out[0] = BBLE_MAGIC;
        out[1] = BBLE_FRAME_ACK;
        out[2] = dev->seq++;
        out[3] = dev->ack_cmd;
        out[4] = dev->ack_res;
        if (finished) dev->status_pending = true; /* final reading goes out next tick */
        return BBLE_ACK_LEN;
    }
    if (finished) return write_reading(dev, out, cap, true);
    if (dev->status_pending) {
        dev->status_pending = false;
        return write_reading(dev, out, cap, dev->state == BBLE_DONE);
    }
    if (dev->state == BBLE_SAMPLING && dev->ticks % 2 == 0) return write_reading(dev, out, cap, false);
    if ((dev->state == BBLE_IDLE || dev->state == BBLE_DONE || dev->state == BBLE_CALIBRATING) &&
        dev->ticks % BBLE_IDLE_BEAT_TICKS == 0) {
        return write_reading(dev, out, cap, dev->state == BBLE_DONE);
    }
    return 0;
}

const char *bble_state_name(uint8_t state)
{
    switch (state) {
    case BBLE_IDLE: return "idle";
    case BBLE_SAMPLING: return "sampling";
    case BBLE_CALIBRATING: return "calibrating";
    case BBLE_DONE: return "done";
    default: return "unknown";
    }
}
