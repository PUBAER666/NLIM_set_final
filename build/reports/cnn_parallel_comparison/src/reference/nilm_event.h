#ifndef NILM_EVENT_H
#define NILM_EVENT_H
#include "nilm_infer.h"
#include "nilm_phase.h"
#include "nilm_lag.h"
#include <stddef.h>
enum {NE_IDLE,NE_BEFORE,NE_ARMED,NE_SETTLE,NE_AFTER,NE_PREP,NE_PHASE,NE_ALIGN,NE_RESIDUAL,NE_LAG,NE_RECLASS,NE_INFER,NE_DONE,NE_ERROR,NE_CANCELLED};
typedef struct {
    unsigned checkpoint,checked,failed_mask,first_id[6];
    double power[6],mean[2],stddev[2],limit[2];
    const char *reason;
} ne_source_diag;
typedef struct {
    unsigned count,revision,candidate;
    double paired_base,paired_registered,score,margin;
    const char *reason;
} ne_registry_diag;
typedef struct {
    unsigned state,id,blocks,settle,have_last,epoch,last_id;
    uint64_t last_seq,start_ms,last_rx_ms;
    uint64_t arm_ms,mark_ms;
    unsigned mark_last_id;
    unsigned first_id[6],raw_ready[6],prepared[6],flipped[6];
    uint32_t raw[6][NI_N];
    double v[6][NI_N],i[6][NI_N],residual[3][NI_N];
    double power[6],vv[6],mean_v,mean_i,dot,best,ss,si,drop;
    unsigned window,index,prep_phase,pair,ready[3],residual_ready[3];
    int shift,best_shift,shifts[3];
    double corr[3],rp[3],ri[3],power_error[3],confidence[3];
    double features[3][5];
    float embeddings[3][64];
    unsigned recognition[3],direction;
    double support_score[3],candidate_score[3];
    double embedding_score[3],auxiliary_score[3],auxiliary_probability[3];
    unsigned auxiliary_bypassed[3];
    unsigned support_match[3],support_calibrated[3];
    const char *reject_reason[3];
    unsigned labels[3],routes[3],knn[3],cnn[3],votes,label,valid;
    const char *reason;
    ne_registry_diag registry[3];
    ne_source_diag source,checkpoints[6];
    unsigned checkpoint_count;
    const char *last_failure;
    nph_job phase_job;
    ng_lag lag_job;
    ni_engine engine;
    nm_result result;
} ne_event;
void ne_init(ne_event *);
int ne_busy(const ne_event *);
void ne_fail(ne_event *,const char *);
void ne_reset_results(ne_event *);
void ne_record_checkpoint(ne_event *,unsigned,const char *);
int ne_diagnostics_json(const ne_event *,char *,size_t);
void ne_push(ne_event *,const nilm_stream_desc *,const nilm_stream_sample *,uint64_t);
void ne_poll(ne_event *,uint64_t);
int ne_json(const ne_event *,char *,size_t);
const char *ne_identification(const ne_event *);
const char *ne_public_label(const ne_event *);
int ne_command(ne_event *,const char *,char *,size_t,uint64_t,int);
#endif
