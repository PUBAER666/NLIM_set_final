#include "nilm_stream.h"
#include <string.h>
#define READ(a) s->io.read_csr(s->io.user,(a))
#define WRITE(a,v) s->io.write_csr(s->io.user,(a),(v))
static void fail(nilm_stream *s, uint32_t error) { s->error=error; s->state=NS_ERROR; }
static void command(nilm_stream *s, uint32_t op, uint32_t now)
{
    WRITE(0x020,op);
    WRITE(0x024,op==3?s->desc.bank:0);
    WRITE(0x028,op==3?(uint32_t)s->desc.sequence:0);
    WRITE(0x02c,op==3?(uint32_t)(s->desc.sequence>>32):0);
    WRITE(0x030,op==3?s->desc.epoch:0);
    ++s->command_seq; WRITE(0x034,s->command_seq);
    WRITE(0x038,0x434d4431u);
    s->command_ms=now; s->command_kind=op; s->state=NS_COMMAND;
}
int nilm_stream_init(nilm_stream *s,const nilm_stream_io *io,nilm_stream_sample *buffer,uint32_t now)
{
    if(!s||!io||!buffer||!io->read_csr||!io->write_csr||!io->read_ram) return -1;
    memset(s,0,sizeof *s); s->io=*io; s->buffer=buffer;
    if(READ(0)!=0x4e344231u||READ(4)!=0x10000u||READ(8)!=NILM_STREAM_SAMPLES) { fail(s,NS_ABI); return -1; }
    s->hw_faults=READ(0x018);
    if(s->hw_faults) { fail(s,NS_HW_FAULT); return -1; }
    if((READ(0x00c)&1u)||READ(0x014)) { fail(s,NS_OWNED); return -1; }
    s->epoch=READ(0x010); s->command_seq=READ(0x040); s->progress_ms=now;
    command(s,1,now); return 0;
}
static int same_descriptor(nilm_stream *s)
{
    uint32_t base=0x100u+s->desc.bank*64u;
    return READ(base)==1 && READ(base+4)==s->desc.epoch &&
           READ(base+8)==(uint32_t)s->desc.sequence &&
           READ(base+12)==(uint32_t)(s->desc.sequence>>32);
}
void nilm_stream_stop(nilm_stream *s,uint32_t now)
{
    if(s->state==NS_SELECT&&!s->stopping) { s->stopping=1; command(s,2,now); }
}
void nilm_stream_poll(nilm_stream *s,uint32_t now)
{
    if(s->state==NS_ERROR||s->state==NS_STOPPED||s->state==NS_IDLE) return;
    s->hw_faults=READ(0x018);
    if(s->hw_faults) { fail(s,NS_HW_FAULT); return; }
    if(s->state==NS_COMMAND) {
        if(READ(0x048) && READ(0x040)==s->command_seq) {
            s->ack_result=READ(0x044);
            if(s->ack_result) { fail(s,NS_CMD_FAILED); return; }
            if(s->command_kind==1) {
                uint32_t epoch=READ(0x010);
                if(epoch!=s->epoch+1u) { fail(s,NS_RESET); return; }
                s->epoch=epoch; s->state=NS_SELECT;
            } else if(s->command_kind==3) {
                if(READ(0x010)!=s->epoch) { fail(s,NS_RESET); return; }
                ++s->blocks; s->have_block=1; s->last_id=s->desc.last_id;
                s->gap_blocks+=s->desc.sequence-s->expected_seq;
                s->expected_seq=s->desc.sequence+1; s->state=NS_BLOCK;
            } else s->state=NS_SELECT;
            s->progress_ms=now;
        } else if((uint32_t)(now-s->command_ms)>1000u) fail(s,NS_CMD_TIMEOUT);
        return;
    }
    if(READ(0x010)!=s->epoch) { fail(s,NS_RESET); return; }
    if(s->state==NS_BLOCK) s->state=NS_SELECT; // caller changes DDR destination before next poll
    if(s->state==NS_SELECT) {
        uint32_t ready=READ(0x014); int found=0;
        for(uint32_t bank=0;bank<4;bank++) if(ready&(1u<<bank)) {
            uint32_t base=0x100u+bank*64u;
            nilm_stream_desc d;
            d.bank=bank; d.epoch=READ(base+4);
            d.sequence=READ(base+8); d.sequence|=(uint64_t)READ(base+12)<<32;
            d.first_id=READ(base+16); d.last_id=READ(base+20);
            if(d.epoch!=s->epoch||READ(base+24)!=NILM_STREAM_SAMPLES||READ(base+28)!=1||READ(base)!=1) { fail(s,NS_DESC); return; }
            if(!found||d.sequence<s->desc.sequence) { s->desc=d; found=1; }
        }
        if(!found) {
            if(s->stopping && !(READ(0x00c)&1u)) s->state=NS_STOPPED;
            else if((uint32_t)(now-s->progress_ms)>1000u) fail(s,NS_STALLED);
            return;
        }
        if(s->desc.sequence<s->expected_seq ||
           s->desc.last_id-s->desc.first_id!=NILM_STREAM_SAMPLES-1u) { fail(s,NS_DESC); return; }
        if(s->have_block && s->desc.first_id!=s->last_id+1u+
             (uint32_t)((s->desc.sequence-s->expected_seq)*NILM_STREAM_SAMPLES)) { fail(s,NS_DATA); return; }
        s->copied=0; s->state=NS_COPY;
    }
    if(s->state==NS_COPY) {
        if(!same_descriptor(s)) { fail(s,NS_DESC); return; }
        uint32_t limit=s->copied+128u;
        if(limit>NILM_STREAM_SAMPLES) limit=NILM_STREAM_SAMPLES;
        for(;s->copied<limit;s->copied++) {
            uint32_t offset=s->desc.bank*8192u+s->copied*8u;
            nilm_stream_sample *p=&s->buffer[s->copied];
            p->data=s->io.read_ram(s->io.user,offset);
            p->id=s->io.read_ram(s->io.user,offset+4);
            if((p->data&0xf000f000u)||p->id!=s->desc.first_id+s->copied) { fail(s,NS_DATA); return; }
        }
        if(s->copied==NILM_STREAM_SAMPLES) {
            if(READ(0x010)!=s->epoch||READ(0x018)||!same_descriptor(s)) { fail(s,NS_DESC); return; }
            command(s,3,now); // DDR copy verified before ownership release
        }
    }
}
