/* One accelerator, private DMA staging. Cancellation drains the old transfer;
 * the caller may immediately reuse its own workspace. Timeout latches a fault
 * and retains DMA memory forever, until a full board reset. */
#include "nilm_pl.h"
#include "xil_io.h"
#include "xil_cache.h"
#include "xil_mmu.h"
#include <math.h>
#include <string.h>
#ifndef NILM_CNN_BASE
#error Build only against the generated CNN hardware platform
#endif
static float input_dma[4096] __attribute__((aligned(64)));
static float output_dma[80] __attribute__((aligned(64)));
static void *owner;
static unsigned busy,ready,fault,enabled;
static uint64_t started;
static uint64_t (*clock_ms)(void);
static uint32_t rd(unsigned a){return Xil_In32(NILM_CNN_BASE+a);}
static void wr(unsigned a,uint32_t x){Xil_Out32(NILM_CNN_BASE+a,x);}
void np_init(uint64_t (*clock)(void)) {
    clock_ms=clock;owner=0;busy=ready=fault=0;
    Xil_SetTlbAttributes(NILM_CNN_BASE,DEVICE_MEMORY);
    enabled=(rd(0)&4u)!=0; /* idle, never start auto-restart */
    wr(4,0);wr(8,0);
}
int np_available(void){return enabled && !fault;}
void np_tick(void) {
    if(!busy || fault)return;
    if(rd(0)&2u) {
        dsb();Xil_DCacheInvalidateRange((INTPTR)output_dma,sizeof(output_dma));dsb();
        busy=0;ready=owner!=0;
        if(rd(0x38)!=0)fault=1;
        for(unsigned n=0;n<78;n++)if(!isfinite(output_dma[n]))fault=1;
    } else if(clock_ms()-started>500){fault=1;ready=0;}
}
int np_submit(void *who,const float *input) {
    np_tick();if(!who || !np_available())return -1;
    if(busy || owner)return 0;
    for(unsigned n=0;n<4096;n++)if(!isfinite(input[n]))return -1;
    memcpy(input_dma,input,sizeof(input_dma));
    for(unsigned n=0;n<80;n++)output_dma[n]=NAN;
    Xil_DCacheFlushRange((INTPTR)input_dma,sizeof(input_dma));
    Xil_DCacheFlushRange((INTPTR)output_dma,sizeof(output_dma));dsb();
    wr(0x10,(uint32_t)(UINTPTR)input_dma);wr(0x14,0);
    wr(0x1c,(uint32_t)(UINTPTR)output_dma);wr(0x20,0);
    wr(0x28,2);wr(0x30,0x37ded17f);
    owner=who;busy=1;ready=0;started=clock_ms();dsb();wr(0,1);return 1;
}
int np_poll(void *who,float *logits,float *embedding) {
    np_tick();if(!np_available() || who!=owner)return -1;
    if(!ready)return 0;
    memcpy(logits,output_dma,14*sizeof(float));memcpy(embedding,output_dma+14,64*sizeof(float));owner=0;ready=0;return 1;
}
void np_cancel(void *who){if(owner==who){owner=0;ready=0;}np_tick();}
