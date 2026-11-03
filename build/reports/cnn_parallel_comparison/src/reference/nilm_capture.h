#ifndef NILM_CAPTURE_H
#define NILM_CAPTURE_H

#include <stdint.h>

/* Legacy GPIO/BRAM ABI. Sample IDs count successful reader outputs, not time. */
#define NILM_CAPTURE_SAMPLES 4096u
#define NILM_STATUS_BUSY     (1u << 0)
#define NILM_STATUS_FULL     (1u << 1)
#define NILM_STATUS_DROPPED  (1u << 2)
#define NILM_STATUS_OVERRUN  (1u << 3)
#define NILM_STATUS_ACK      (1u << 4)
#define NILM_STATUS_READY    (1u << 5)

typedef struct {
    uint32_t data; /* voltage [27:16], current [11:0], padding zero */
    uint32_t sample_id;
} nilm_sample;

typedef struct {
    void *user;
    uint32_t (*status)(void *user);
    uint32_t (*control)(void *user);
    void (*write_control)(void *user, uint32_t value);
    uint32_t (*read_word)(void *user, uint32_t byte_offset);
} nilm_capture_io;

typedef enum {
    NILM_CAP_IDLE, NILM_CAP_WAIT_IDLE, NILM_CAP_WAIT_ACK,
    NILM_CAP_ACQUIRING, NILM_CAP_COPYING, NILM_CAP_DONE, NILM_CAP_ERROR
} nilm_capture_state;

typedef enum {
    NILM_CAP_OK, NILM_CAP_IDLE_TIMEOUT, NILM_CAP_ACK_TIMEOUT,
    NILM_CAP_FRAME_TIMEOUT, NILM_CAP_COPY_TIMEOUT, NILM_CAP_RESET,
    NILM_CAP_STATUS_CHANGED, NILM_CAP_SOURCE_OVERRUN,
    NILM_CAP_WRITER_DROPPED, NILM_CAP_BAD_DATA
} nilm_capture_error;

typedef struct {
    uint32_t samples, first_id, last_id, sequence_errors, padding_errors;
    uint32_t last_status, elapsed_ms;
    uint16_t voltage_min, voltage_max, current_min, current_max;
} nilm_capture_stats;

typedef struct {
    nilm_capture_io io;
    nilm_sample *buffer;
    uint32_t sample_count, copied, request, started_ms, phase_ms;
    nilm_capture_state state;
    nilm_capture_error error;
    nilm_capture_stats stats;
} nilm_capture;

/* Each poll reads at most 128 sample pairs. Single foreground owner only.
 * Caller owns buffer and must not start again while consuming the last frame.
 * now_ms is monotonic modulo 2^32. Poll at least once per 160 ms capture.
 * Timeout never toggles again or resets hardware: a new explicit start first
 * waits for the old request to settle. A stuck PL needs coordinated PL reset.
 */
int nilm_capture_init(nilm_capture *cap, const nilm_capture_io *io,
                      nilm_sample *buffer, uint32_t samples);
int nilm_capture_start(nilm_capture *cap, uint32_t now_ms);
void nilm_capture_poll(nilm_capture *cap, uint32_t now_ms);
const char *nilm_capture_error_name(nilm_capture_error error);

#endif
