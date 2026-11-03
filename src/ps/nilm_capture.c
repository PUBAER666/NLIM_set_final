#include "nilm_capture.h"
#include <string.h>

#define WAIT_TIMEOUT_MS 1000u
#define FRAME_TIMEOUT_MS 1000u
#define COPY_TIMEOUT_MS 1000u
#define COPY_PAIRS_PER_POLL 128u

static void fail(nilm_capture *c, nilm_capture_error e, uint32_t now)
{
    c->error = e;
    c->state = NILM_CAP_ERROR;
    c->stats.elapsed_ms = now - c->started_ms;
}

static int ack_matches(const nilm_capture *c, uint32_t s)
{
    return ((s & NILM_STATUS_ACK) != 0u) == (c->request != 0u);
}

static int frozen(const nilm_capture *c, uint32_t s)
{
    return (s & (NILM_STATUS_READY | NILM_STATUS_BUSY | NILM_STATUS_FULL)) ==
           (NILM_STATUS_READY | NILM_STATUS_FULL) && ack_matches(c, s);
}

int nilm_capture_init(nilm_capture *c, const nilm_capture_io *io,
                      nilm_sample *buffer, uint32_t samples)
{
    if (!c || !io || !buffer || !io->status || !io->control ||
        !io->write_control || !io->read_word || !samples ||
        samples > NILM_CAPTURE_SAMPLES)
        return -1;
    memset(c, 0, sizeof(*c));
    c->io = *io;
    c->buffer = buffer;
    c->sample_count = samples;
    return 0;
}

int nilm_capture_start(nilm_capture *c, uint32_t now)
{
    if (c->state != NILM_CAP_IDLE && c->state != NILM_CAP_DONE &&
        c->state != NILM_CAP_ERROR)
        return -1;
    memset(&c->stats, 0, sizeof(c->stats));
    c->stats.voltage_min = c->stats.current_min = 4095u;
    c->copied = 0;
    c->started_ms = c->phase_ms = now;
    c->error = NILM_CAP_OK;
    c->state = NILM_CAP_WAIT_IDLE;
    return 0;
}

void nilm_capture_poll(nilm_capture *c, uint32_t now)
{
    uint32_t s, control, limit;
    if (c->state == NILM_CAP_IDLE || c->state == NILM_CAP_DONE ||
        c->state == NILM_CAP_ERROR)
        return;
    s = c->io.status(c->io.user);
    c->stats.last_status = s;

    if (c->state == NILM_CAP_WAIT_IDLE) {
        if (now - c->phase_ms >= WAIT_TIMEOUT_MS) {
            fail(c, NILM_CAP_IDLE_TIMEOUT, now);
            return;
        }
        control = c->io.control(c->io.user);
        /* Never generate another toggle while the previous request is in flight. */
        if (!(s & NILM_STATUS_READY) || (s & NILM_STATUS_BUSY) ||
            (((s & NILM_STATUS_ACK) != 0u) != ((control & 1u) != 0u)))
            return;
        if (s & NILM_STATUS_OVERRUN) {
            fail(c, NILM_CAP_SOURCE_OVERRUN, now);
            return;
        }
        c->request = (control ^ 1u) & 1u;
        c->io.write_control(c->io.user, (control & ~1u) | c->request);
        c->phase_ms = now;
        c->state = NILM_CAP_WAIT_ACK;
        return;
    }

    if (!(s & NILM_STATUS_READY)) {
        fail(c, NILM_CAP_RESET, now);
        return;
    }
    if (s & NILM_STATUS_OVERRUN) {
        fail(c, NILM_CAP_SOURCE_OVERRUN, now);
        return;
    }
    if (c->state == NILM_CAP_WAIT_ACK) {
        /* The old FULL can survive several CDC clocks. Require evidence of
         * this capture starting; matching ACK + FULL alone is insufficient. */
        if (now - c->phase_ms >= WAIT_TIMEOUT_MS) {
            fail(c, NILM_CAP_ACK_TIMEOUT, now);
        } else if (ack_matches(c, s) && (s & NILM_STATUS_BUSY) &&
                   !(s & NILM_STATUS_FULL)) {
            c->state = NILM_CAP_ACQUIRING;
            /* Keep phase_ms at request time; no new full timeout after ACK. */
        }
        return;
    }
    if (!ack_matches(c, s)) {
        fail(c, NILM_CAP_STATUS_CHANGED, now);
        return;
    }
    if (s & NILM_STATUS_DROPPED) {
        fail(c, NILM_CAP_WRITER_DROPPED, now);
        return;
    }
    if (c->state == NILM_CAP_ACQUIRING) {
        if (now - c->phase_ms >= FRAME_TIMEOUT_MS) {
            fail(c, NILM_CAP_FRAME_TIMEOUT, now);
        } else if (frozen(c, s)) {
            c->state = NILM_CAP_COPYING;
            c->phase_ms = now;
        }
        return;
    }
    if (now - c->phase_ms >= COPY_TIMEOUT_MS) {
        fail(c, NILM_CAP_COPY_TIMEOUT, now);
        return;
    }
    if (!frozen(c, s)) {
        fail(c, NILM_CAP_STATUS_CHANGED, now);
        return;
    }

    limit = c->copied + COPY_PAIRS_PER_POLL;
    if (limit > c->sample_count) limit = c->sample_count;
    for (; c->copied < limit; ++c->copied) {
        nilm_sample *p = &c->buffer[c->copied];
        uint16_t v, i;
        p->data = c->io.read_word(c->io.user, c->copied * 8u);
        p->sample_id = c->io.read_word(c->io.user, c->copied * 8u + 4u);
        if (p->data & 0xf000f000u) ++c->stats.padding_errors;
        if (c->copied == 0u) c->stats.first_id = p->sample_id;
        else if (p->sample_id != c->stats.last_id + 1u)
            ++c->stats.sequence_errors; /* Modulo-2^32 wrap is valid. */
        c->stats.last_id = p->sample_id;
        v = (uint16_t)((p->data >> 16) & 0xfffu);
        i = (uint16_t)(p->data & 0xfffu);
        if (v < c->stats.voltage_min) c->stats.voltage_min = v;
        if (v > c->stats.voltage_max) c->stats.voltage_max = v;
        if (i < c->stats.current_min) c->stats.current_min = i;
        if (i > c->stats.current_max) c->stats.current_max = i;
    }
    c->stats.samples = c->copied;
    if (c->copied == c->sample_count) {
        /* Check again after the final MMIO reads before publishing the frame. */
        s = c->io.status(c->io.user);
        c->stats.last_status = s;
        if (!frozen(c, s)) fail(c, NILM_CAP_STATUS_CHANGED, now);
        else if (s & NILM_STATUS_OVERRUN) fail(c, NILM_CAP_SOURCE_OVERRUN, now);
        else if (s & NILM_STATUS_DROPPED) fail(c, NILM_CAP_WRITER_DROPPED, now);
        else if (c->stats.sequence_errors || c->stats.padding_errors)
            fail(c, NILM_CAP_BAD_DATA, now);
        else {
            c->state = NILM_CAP_DONE;
            c->stats.elapsed_ms = now - c->started_ms;
        }
    }
}

const char *nilm_capture_error_name(nilm_capture_error e)
{
    static const char *const names[] = {
        "ok", "idle_timeout", "ack_timeout", "frame_timeout", "copy_timeout",
        "pl_reset", "status_changed", "source_overrun", "writer_dropped", "bad_data"
    };
    return (unsigned)e < sizeof(names) / sizeof(names[0]) ? names[e] : "unknown";
}
