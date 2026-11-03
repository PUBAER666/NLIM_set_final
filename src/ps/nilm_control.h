#ifndef NILM_CONTROL_H
#define NILM_CONTROL_H
#include <stddef.h>
int nilm_control_init(void);
void nilm_control_poll(void);
int nilm_event_request(const char *,char *,size_t);
#endif
