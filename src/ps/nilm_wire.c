#include "nilm_wire.h"
#include <string.h>
static void put32(unsigned char *p,uint32_t n) { unsigned i;for(i=0;i<4;i++) p[i]=(unsigned char)(n>>(8*i)); }
static void put64(unsigned char *p,uint64_t n) {put32(p,(uint32_t)n);put32(p+4,(uint32_t)(n>>32));}
static uint32_t crc(uint32_t c,const unsigned char *p,uint32_t n)
{
    static const uint32_t t[16]={0,0x1db71064,0x3b6e20c8,0x26d930ac,0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c};
    while(n--) { c^=*p++;c=(c>>4)^t[c&15];c=(c>>4)^t[c&15]; }
    return c;
}
void nw_reset(nw_queue *q) {q->head=0;q->count=0;q->offset=0;q->dropped=0;}
int nw_push(nw_queue *q,const nilm_stream_desc *d,const nilm_stream_sample *s,uint64_t ms,uint64_t gaps)
{
    unsigned i;unsigned char *p;uint32_t c;
    if(q->count==NW_DEPTH) {if(q->dropped!=UINT64_MAX)q->dropped++;return 0;}
    p=q->frame[(q->head+q->count)%NW_DEPTH];memset(p,0,NW_HEADER);
    memcpy(p,"NLM1",4);p[4]=1;p[6]=NW_HEADER;
    put32(p+8,NILM_STREAM_SAMPLES*8u);put32(p+12,1);put32(p+16,d->epoch);
    put32(p+20,NILM_STREAM_SAMPLES);put64(p+24,d->sequence);
    put32(p+32,d->first_id);put32(p+36,d->last_id);put32(p+40,25600);
    put32(p+44,3); /* bit0 uncalibrated ADC; bit1 RAW1 success IDs, not time slots */
    put64(p+48,ms);put64(p+56,gaps);put64(p+64,q->dropped);
    for(i=0;i<NILM_STREAM_SAMPLES;i++) {put32(p+NW_HEADER+i*8,s[i].data);put32(p+NW_HEADER+i*8+4,s[i].id);}
    c=crc(UINT32_MAX,p,76);c=crc(c,p+NW_HEADER,NW_BYTES-NW_HEADER);
    put32(p+76,c^UINT32_MAX);q->count++;return 1;
}
const unsigned char *nw_peek(nw_queue *q,uint32_t *n)
{*n=q->count?NW_BYTES-q->offset:0;return q->count?q->frame[q->head]+q->offset:0;}
void nw_consume(nw_queue *q,uint32_t n)
{
    if(!q->count || n>NW_BYTES-q->offset)return;
    q->offset+=n;
    if(q->offset==NW_BYTES) {q->offset=0;q->head=(q->head+1)%NW_DEPTH;q->count--;}
}
