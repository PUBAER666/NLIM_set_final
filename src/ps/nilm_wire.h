#ifndef NILM_WIRE_H
#define NILM_WIRE_H
#include "nilm_stream.h"
#define NW_HEADER 80u
#define NW_BYTES (NW_HEADER+NILM_STREAM_SAMPLES*8u)
#define NW_DEPTH 8u
typedef struct {
    unsigned char frame[NW_DEPTH][NW_BYTES];
    uint32_t head, count, offset;
    uint64_t dropped;
} nw_queue;
void nw_reset(nw_queue *);
int nw_push(nw_queue *,const nilm_stream_desc *,const nilm_stream_sample *,uint64_t,uint64_t);
const unsigned char *nw_peek(nw_queue *,uint32_t *);
void nw_consume(nw_queue *,uint32_t);
#endif
