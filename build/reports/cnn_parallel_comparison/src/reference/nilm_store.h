#ifndef NILM_STORE_H
#define NILM_STORE_H
#include "nilm_learn.h"
// active changes only after a durable, verified commit to the inactive SD slot.
extern nl_bank ns_active,ns_pending;
void ns_init(void);
int ns_begin(void);
void ns_poll(void);
int ns_busy(void);
int ns_ready(void);
const char *ns_status(void);
#endif
