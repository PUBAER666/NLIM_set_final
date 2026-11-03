#ifndef NILM_BOARD_H
#define NILM_BOARD_H
#include "nilm_event.h"
enum {NB_WARMUP,NB_ARMED,NB_CONFIRMING,NB_COOLDOWN};
typedef struct {double p,v,i;} nb_metric;
typedef struct {
    uint64_t ms,sequence;
    unsigned phase;
    const char *reason;
    double power,baseline,threshold;
} nb_trace;
typedef struct {
    ne_event event;
    uint32_t raw[64][1024];
    uint32_t background[3][NI_N],background_ids[3];
    uint64_t stable_sequence,background_ms;
    unsigned stable_valid,background_locked,background_valid,quiet_blocks;
    nilm_stream_desc desc[64],previous;
    nb_metric history[25],pending[8];
    unsigned have_last,hcount,hnext,pcount,phase,cooldown,unstable;
    unsigned direction,checkpoint,post_copied,previous_signature,previous_agree;
    uint64_t post_first;
    unsigned enabled,collecting,event_count,busy_drops,power_valid;
    uint64_t trigger,trigger_ms,confirm,valid_blocks,bad_blocks,gaps,event_observed,last_input_ms;
    uint32_t trigger_first_id;
    double baseline,baseline_v,threshold,power,vrms,irms,energy_wh,carbon_kg,factor;
    unsigned factor_set;
    unsigned confirm_blocks,confirm_retries,confirm_cancelled,confirm_timeouts;
    unsigned trace_next,trace_count;
    nb_trace trace[16];
} nb_board;
void nb_registry_changed(nb_board *);
void nb_init(nb_board *);
void nb_push(nb_board *,const nilm_stream_desc *,const nilm_stream_sample *,uint64_t);
void nb_poll(nb_board *,uint64_t);
int nb_set_factor(nb_board *,double);
int nb_json(const nb_board *,char *,size_t);
#endif
