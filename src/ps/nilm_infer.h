#ifndef NILM_INFER_H
#define NILM_INFER_H
#include "nilm_model.h"
#include "nilm_dynamic.h"
#define NI_N 4096u
#define NI_CLASSES 14u
enum { NI_IDLE,NI_SUM,NI_PREPROCESS,NI_KNN,NI_CONV1,NI_POOL1,NI_CONV2,NI_POOL2,NI_CONV3,NI_GLOBAL,NI_FC,NI_POLARITY,NI_PHYSICAL,NI_PL_WAIT,NI_DYNAMIC };
enum { NI_ROUTE_KNN=1,NI_ROUTE_CNN_KEEP,NI_ROUTE_CNN_OVERRIDE,NI_ROUTE_REGISTERED };
typedef struct {
    const nilm_stream_sample *raw;
    const double *physical_v,*physical_i;
    float current[NI_N],a[32768],b[32768],embedding[64],logits[NI_CLASSES];
    double sum_v,sum_i,sum_vv,sum_ii,sum_vi,peak,features[5],query[5],class_distance[NI_CLASSES];
    double d1,ratio,vrms,irms,power;
    float cnn_confidence;
    uint32_t cnn_label,polarity_flipped,direct_cnn;
    uint32_t stage,index,knn_label,label,route,rail_samples,cnn_status;
    uint64_t job,completed,cnn_calls,cnn_overrides,cancelled;
    uint32_t last_epoch,last_first_id,last_last_id;
    uint64_t last_sequence;
    int result_valid;
    unsigned extended; nd_job dynamic;
} ni_engine;
int ni_submit(void *,const nm_window *,nm_result *);
/* Already calibrated, centered immutable physical arrays; retained until done/cancel. */
int ni_submit_physical(ni_engine *,const double *,const double *,uint64_t,nm_result *);
int ni_poll(void *,nm_result *);
void ni_cancel(void *);
/* Diagnostics-only host entry: current already uses the trained input units. */
int ni_start_cnn(ni_engine *,const float *);
int ni_selftest(ni_engine *,nilm_stream_sample *);
#endif
