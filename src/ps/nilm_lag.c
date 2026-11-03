#include "nilm_lag.h"
#include "nilm_lag_assets.h"
#include <math.h>
#include <float.h>
#include <string.h>
#include <stdlib.h>
void ng_begin(ng_lag *j,const double *v,const float *i,const float *embedding){
    memset(j,0,sizeof(*j));j->v=v;j->i=i;j->embedding=embedding;j->lag=-30;j->index=30;j->best=-2;
    for(unsigned n=0;n<15;n++)j->distance[n]=DBL_MAX;
}
int ng_poll(ng_lag *j){
    if(j->phase==0){
        unsigned stop=j->lag>0?4096-j->lag:4096,end=j->index+128;if(end>stop)end=stop;
        for(;j->index<end;j->index++){double v=j->v[j->index],i=j->i[(int)j->index+j->lag];j->dot+=v*i;j->vv+=v*v;j->ii+=i*i;}
        if(j->index==stop){
            double corr=j->dot/(sqrt(j->vv*j->ii)+1e-12);
            if(corr>j->best+1e-12||(fabs(corr-j->best)<=1e-12&&abs(j->lag)<abs(j->best_lag))){j->best=corr;j->best_lag=j->lag;}
            j->dot=j->vv=j->ii=0;j->lag++;j->index=j->lag<0?(unsigned)-j->lag:0;
            if(j->lag>30){j->index=0;j->phase=j->best>=.985?1:2;}
        }return 0;
    }
    if(j->phase==1){
        unsigned end=j->row+16;if(end>NG_LAG_ROWS)end=NG_LAG_ROWS;
        for(;j->row<end;j->row++){
            double dist=0;for(unsigned n=0;n<64;n++){double x=(ng_lag_refs[j->row*64+n]-j->embedding[n])/ng_lag_std[n];dist+=x*x;}
            unsigned at=15;while(at&&dist<j->distance[at-1])at--;
            if(at<15){for(unsigned k=14;k>at;k--){j->distance[k]=j->distance[k-1];j->lags[k]=j->lags[k-1];}j->distance[at]=dist;j->lags[at]=ng_lag_values[j->row];}
        }
        if(j->row==NG_LAG_ROWS){
            for(unsigned n=1;n<15;n++){double x=j->lags[n];unsigned k=n;while(k&&j->lags[k-1]>x){j->lags[k]=j->lags[k-1];k--;}j->lags[k]=x;}
            j->shift=(int)nearbyint(j->best_lag-j->lags[7]);if(j->shift>12)j->shift=12;if(j->shift< -12)j->shift=-12;j->phase=2;
        }return 0;
    }
    if(j->phase==2){
        unsigned end=j->index+128;if(end>4096)end=4096;
        for(;j->index<end;j->index++){unsigned at=(j->index+4096-j->shift)%4096;j->power+=j->v[at]*j->i[j->index]/4096;}
        if(j->index==4096){j->power=fabs(j->power);j->phase=3;return 1;}return 0;
    }return 1;
}
