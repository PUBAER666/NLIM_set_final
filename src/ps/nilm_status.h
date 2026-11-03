#ifndef NILM_STATUS_H
#define NILM_STATUS_H
#include <stddef.h>
#define NILM_FIRMWARE_VERSION "NLIM_set_fianl"
int nilm_status_init(void);
void nilm_status_poll(void);
int nilm_device_json(char *,size_t);
int nilm_net_ready(void);
int nilm_net_connected(void);
#endif
