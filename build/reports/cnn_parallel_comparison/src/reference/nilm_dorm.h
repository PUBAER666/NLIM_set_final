#ifndef NILM_DORM_H
#define NILM_DORM_H
#include "nilm_board.h"
int dorm_registry_sync(nb_board *,uint64_t);
void dorm_init(void);
void dorm_block(const nb_board *,uint64_t);
void dorm_poll(const nb_board *,uint64_t);
int dorm_json(const nb_board *,char *,size_t,uint64_t);
int dorm_command(const nb_board *,const char *,char *,size_t,uint64_t);
#endif
