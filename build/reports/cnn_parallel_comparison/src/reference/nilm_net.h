#ifndef NILM_NET_H
#define NILM_NET_H
#include "nilm_stream.h"
int nilm_net_init(void);
void nilm_net_poll(void);
void nilm_net_push(const nilm_stream_desc *,const nilm_stream_sample *,uint64_t,uint64_t);
void nilm_net_stop(void);
#endif
