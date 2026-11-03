#ifndef NILM_DYNAMIC_H
#define NILM_DYNAMIC_H
#include "nilm_learn.h"
#include "nilm_open.h"
#define ND_CLASSES (14+NL_PROFILES)
enum {ND_KNOWN,ND_UNKNOWN,ND_AMBIGUOUS,ND_INVALID};
typedef struct {
    double q5[5],q64[64],d5[ND_CLASSES],d64[ND_CLASSES];
    double d1,d2,e1,e2,confidence,support5,support64;
    double support_min;
    unsigned matched,support_calibrated;
    const char *reason;
    unsigned index,phase,revision,label,knn,cnn,called,state;
    no_job open;
} nd_job;
void nd_begin(nd_job *,const double *,const float *);
int nd_poll(nd_job *);
/* Applies support rejection to per-class squared 5D distances. No CNN retraining. */
void nd_assess_support(nd_job *);
const char *nd_name(unsigned);
#endif
