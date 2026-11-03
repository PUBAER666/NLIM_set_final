#ifndef NILM_LAG_H
#define NILM_LAG_H
typedef struct {
    const double *v;const float *i,*embedding;
    unsigned phase,index,row;int lag,best_lag,shift;
    double dot,vv,ii,best,distance[15],lags[15],power;
} ng_lag;
void ng_begin(ng_lag *,const double *,const float *,const float *);
int ng_poll(ng_lag *);
#endif
