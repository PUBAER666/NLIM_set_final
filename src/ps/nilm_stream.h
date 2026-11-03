#ifndef NILM_STREAM_H
#define NILM_STREAM_H
#include <stdint.h>
#define NILM_STREAM_SAMPLES 1024u
typedef struct { uint32_t data, id; } nilm_stream_sample;
typedef struct { uint32_t epoch, bank, first_id, last_id; uint64_t sequence; } nilm_stream_desc;
typedef struct {
    void *user;
    uint32_t (*read_csr)(void *, uint32_t);
    void (*write_csr)(void *, uint32_t, uint32_t);
    uint32_t (*read_ram)(void *, uint32_t);
} nilm_stream_io;
enum { NS_IDLE, NS_COMMAND, NS_SELECT, NS_COPY, NS_BLOCK, NS_STOPPED, NS_ERROR };
enum { NS_OK, NS_ABI, NS_OWNED, NS_HW_FAULT, NS_RESET, NS_CMD_TIMEOUT,
       NS_CMD_FAILED, NS_DESC, NS_DATA, NS_STALLED };
typedef struct {
    nilm_stream_io io;
    nilm_stream_sample *buffer;
    nilm_stream_desc desc;
    uint32_t state, error, epoch, command_seq, command_kind, command_ms;
    uint32_t copied, last_id, progress_ms, hw_faults, ack_result;
    uint64_t expected_seq, blocks, gap_blocks;
    int have_block, stopping;
} nilm_stream;
int nilm_stream_init(nilm_stream *, const nilm_stream_io *, nilm_stream_sample *, uint32_t);
void nilm_stream_poll(nilm_stream *, uint32_t);
void nilm_stream_stop(nilm_stream *, uint32_t);
#endif
