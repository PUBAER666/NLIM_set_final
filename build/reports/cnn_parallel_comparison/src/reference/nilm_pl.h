#ifndef NILM_PL_H
#define NILM_PL_H
#include <stdint.h>
void np_init(uint64_t (*clock_ms)(void));
int np_available(void);
int np_submit(void *owner,const float *input); /* 1 accepted, 0 occupied, -1 fault */
int np_poll(void *owner,float *logits,float *embedding); /* 1 done, 0 running, -1 fault */
void np_cancel(void *owner);
void np_tick(void);
#endif
