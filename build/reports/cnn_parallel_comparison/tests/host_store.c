#include "nilm_store.h"
nl_bank ns_active,ns_pending;
static unsigned busy,ready=1;
int ns_busy(void){return busy;}
int ns_ready(void){return ready&&!busy;}
const char *ns_status(void){return busy?"SAVING":"READY";}
int ns_begin(void){if(!ns_ready()||!nl_validate(&ns_pending))return -1;busy=1;return 0;}
void ns_poll(void){if(busy){ns_active=ns_pending;busy=0;}}
void ns_init(void){nl_init(&ns_active);ns_active.boot_serial=1;ready=1;busy=0;}
