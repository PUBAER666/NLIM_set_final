#include "nilm_infer.h"
#ifdef NI_PL_BACKEND
#include "nilm_pl.h"
#endif
#include "nilm_assets.h"
#include "nilm_golden.h"
#include <float.h>
#include <math.h>
#include <string.h>
/* Frozen training preprocessing. Physical calibration has NOT been validated. */
#define V_GAIN 0.2328271484375
#define I_GAIN 0.015556640625
#define EPS 1e-12
/* Power-on only, before acquisition starts. Never publishes synthetic results. */
int ni_selftest(ni_engine *e,nilm_stream_sample *scratch)
{
    nm_result result={0};int status=NM_BUSY,ok=1;unsigned n;
    memset(e,0,sizeof(*e));
    /* The zero-initialized current workspace is its own input. */
    e->stage=NI_CONV1;e->route=2;e->cnn_status=NM_BUSY;e->direct_cnn=1;
    for(n=0;n<2000000 && status==NM_BUSY;n++)status=ni_poll(e,&result);
    if(status!=NM_DONE)ok=0;
    for(n=0;n<NI_CLASSES;n++)if(!isfinite(e->logits[n]) || fabsf(e->logits[n]-ni_zero_logits[n])>0.001f)ok=0;
    memset(e,0,sizeof(*e));
    for(n=0;n<NI_N;n++){scratch[n].data=(2048u<<16)|2048u;scratch[n].id=n;}
    nm_window w={NM_ABI,1,NI_N,1,0,NI_N-1,0,3,1,scratch};
    status=ni_submit(e,&w,&result);
    for(n=0;n<2000000 && status==NM_BUSY;n++)status=ni_poll(e,&result);
    if(status!=NM_DONE || e->label!=ni_zero_label || e->route!=ni_zero_route || fabs(e->d1-ni_zero_d1)>1e-8)ok=0;
    if(!no_selftest())ok=0;
    memset(e,0,sizeof(*e));return ok;
}
static int finish(ni_engine *e,nm_result *r,int rejected)
{
    e->completed++;e->stage=NI_IDLE;e->raw=0;e->result_valid=1;
    r->job_id=e->job;r->model_version=NI_MODEL_VERSION;r->preprocess_version=NI_PREPROCESS_VERSION;
    r->label=e->label;r->vote_count=0;
    return rejected?NM_REJECTED:NM_DONE;
}
void ni_cancel(void *context)
{
    ni_engine *e=context;
#ifdef NI_PL_BACKEND
    np_cancel(e);
#endif
    if(e->stage!=NI_IDLE)e->cancelled++;
    e->stage=NI_IDLE;e->raw=0;e->result_valid=0;e->cnn_status=NM_NOT_READY;
}
int ni_submit(void *context,const nm_window *w,nm_result *r)
{
    ni_engine *e=context;unsigned c;
    if(e->stage!=NI_IDLE || !w || !w->samples || w->abi!=NM_ABI || w->format!=1 || w->count!=NI_N)return NM_ERROR;
    e->raw=w->samples;e->job=w->job_id;e->last_epoch=w->epoch;e->last_first_id=w->first_id;
    e->last_last_id=w->last_id;e->last_sequence=w->last_block;
    e->sum_v=e->sum_i=e->sum_vv=e->sum_ii=e->sum_vi=e->peak=0;
    e->index=e->rail_samples=0;e->stage=NI_SUM;e->result_valid=0;
    e->route=0;e->label=UINT32_MAX;e->d1=DBL_MAX;e->ratio=0;e->cnn_status=NM_NOT_READY;
    e->cnn_label=UINT32_MAX;e->cnn_confidence=0;e->polarity_flipped=e->direct_cnn=0;
    for(c=0;c<NI_CLASSES;c++)e->class_distance[c]=DBL_MAX;
    r->job_id=w->job_id;r->model_version=NI_MODEL_VERSION;r->preprocess_version=NI_PREPROCESS_VERSION;
    return NM_BUSY;
}
int ni_start_cnn(ni_engine *e,const float *current)
{
    unsigned n;if(e->stage!=NI_IDLE)return -1;
    for(n=0;n<NI_N;n++)if(!isfinite(current[n]))return -1;
    memcpy(e->current,current,sizeof(e->current));e->stage=NI_CONV1;e->index=0;
    e->route=2;e->cnn_status=NM_BUSY;e->cnn_calls++;e->result_valid=0;e->direct_cnn=1;
    return 0;
}
int ni_submit_physical(ni_engine *e,const double *v,const double *i,uint64_t job,nm_result *r)
{
    if(e->stage!=NI_IDLE || !v || !i || !r)return NM_ERROR;
    e->physical_v=v;e->physical_i=i;e->raw=0;e->job=job;
    e->sum_v=e->sum_i=e->sum_vv=e->sum_ii=e->sum_vi=e->peak=0;
    e->index=e->rail_samples=0;e->stage=NI_PHYSICAL;e->result_valid=0;
    e->route=0;e->label=UINT32_MAX;e->d1=DBL_MAX;e->ratio=0;e->cnn_status=NM_NOT_READY;
    e->cnn_label=UINT32_MAX;e->cnn_confidence=0;e->polarity_flipped=e->direct_cnn=0;
    for(unsigned c=0;c<NI_CLASSES;c++)e->class_distance[c]=DBL_MAX;
    r->job_id=job;r->model_version=NI_MODEL_VERSION;r->preprocess_version=NI_PREPROCESS_VERSION;
    return NM_BUSY;
}
static void convolution(ni_engine *e,const float *input,unsigned length,unsigned in_channels,
                        unsigned out_channels,unsigned kernel,const float *weight,const float *bias,
                        const float *alpha,const float *beta,unsigned next)
{
    unsigned end=e->index+32,total=length*out_channels;
    if(end>total)end=total;
    for(;e->index<end;e->index++){
        unsigned oc=e->index/length,pos=e->index%length,ic,k;
        float y=bias[oc];
        for(ic=0;ic<in_channels;ic++)for(k=0;k<kernel;k++){
            int at=(int)pos+(int)k-(int)(kernel/2);
            if(at>=0 && (unsigned)at<length)y+=input[ic*length+(unsigned)at]*weight[(oc*in_channels+ic)*kernel+k];
        }
        y=y*alpha[oc]+beta[oc];e->a[e->index]=y>0?y:0;
    }
    if(e->index==total){e->index=0;e->stage=next;}
}
static void pool(ni_engine *e,unsigned input_length,unsigned channels,unsigned next)
{
    unsigned end=e->index+128,total=channels*input_length/2;
    if(end>total)end=total;
    for(;e->index<end;e->index++){
        float x=e->a[e->index*2],y=e->a[e->index*2+1];e->b[e->index]=x>y?x:y;
    }
    if(e->index==total){e->index=0;e->stage=next;}
}
static int decide(ni_engine *e,nm_result *r)
{
        float best=-FLT_MAX;
        for(unsigned c=0;c<NI_CLASSES;c++) {
            if(!isfinite(e->logits[c])){ni_cancel(e);return NM_ERROR;}
            if(e->logits[c]>best){best=e->logits[c];e->cnn_label=c;}
        }
        float denom=0;
        for(unsigned c=0;c<NI_CLASSES;c++)denom+=expf(e->logits[c]-best);
        if(!isfinite(denom) || denom<1){ni_cancel(e);return NM_ERROR;}
        e->cnn_confidence=1.0f/denom;
        if(e->direct_cnn){e->label=e->cnn_label;}
        else if(e->cnn_label!=e->knn_label && (double)e->cnn_confidence>=0.6357772946){
            e->label=e->cnn_label;e->route=NI_ROUTE_CNN_OVERRIDE;e->cnn_overrides++;
        }else {e->label=e->knn_label;e->route=NI_ROUTE_CNN_KEEP;}
        e->cnn_status=NM_DONE;
        if(e->extended==1 && !e->direct_cnn){nd_begin(&e->dynamic,e->query,e->embedding);e->stage=NI_DYNAMIC;return NM_BUSY;}
        return finish(e,r,0);
}
int ni_poll(void *context,nm_result *r)
{
    ni_engine *e=context;unsigned end,n,c;
    if(e->stage==NI_IDLE)return NM_NOT_READY;
#ifdef NI_PL_BACKEND
    if(e->stage==NI_CONV1) {
        int status=np_submit(e,e->current);
        if(status<0){ni_cancel(e);return NM_ERROR;}
        if(status>0)e->stage=NI_PL_WAIT;
        return NM_BUSY;
    }
    if(e->stage==NI_PL_WAIT) {
        int status=np_poll(e,e->logits,e->embedding);
        if(status<0){ni_cancel(e);return NM_ERROR;}
        return status?decide(e,r):NM_BUSY;
    }
#endif
    if(e->stage==NI_DYNAMIC){
        if(!nd_poll(&e->dynamic))return NM_BUSY;
        e->label=e->dynamic.label;e->knn_label=e->dynamic.knn;e->cnn_label=e->dynamic.cnn;
        e->route=e->dynamic.called?(e->dynamic.label!=e->dynamic.knn?NI_ROUTE_CNN_OVERRIDE:NI_ROUTE_CNN_KEEP):NI_ROUTE_KNN;
        if(e->dynamic.state==ND_KNOWN&&e->label>=14)e->route=NI_ROUTE_REGISTERED;
        return finish(e,r,0);
    }
    if(e->stage==NI_SUM){
        end=e->index+128;if(end>NI_N)end=NI_N;
        for(;e->index<end;e->index++){
            uint32_t d=e->raw[e->index].data,v=(d>>16)&4095u,i=d&4095u;
            e->sum_v+=v;e->sum_i+=i;
            if(v<=4 || v>=4091 || i<=4 || i>=4091)e->rail_samples++;
        }
        if(e->index==NI_N){e->sum_v/=NI_N;e->sum_i/=NI_N;e->stage=NI_PREPROCESS;e->index=0;}
    }else if(e->stage==NI_PREPROCESS || e->stage==NI_PHYSICAL){
        end=e->index+128;if(end>NI_N)end=NI_N;
        for(;e->index<end;e->index++){
            double v,i;
            if(e->stage==NI_PHYSICAL){v=e->physical_v[e->index];i=e->physical_i[e->index];}
            else {uint32_t d=e->raw[e->index].data;
                v=(((d>>16)&4095u)-e->sum_v)*V_GAIN;i=((d&4095u)-e->sum_i)*I_GAIN;}
            if(!isfinite(v) || !isfinite(i)){ni_cancel(e);return NM_ERROR;}
            e->current[e->index]=(float)i;e->sum_vv+=v*v;e->sum_ii+=i*i;e->sum_vi+=v*i;
            if(fabs(i)>e->peak)e->peak=fabs(i);
        }
        if(e->index==NI_N){
            e->vrms=sqrt(e->sum_vv/NI_N);e->irms=sqrt(e->sum_ii/NI_N);e->power=e->sum_vi/NI_N;
            e->polarity_flipped=e->power<0;
            if(e->polarity_flipped)e->power=-e->power;
            e->features[0]=e->power;e->features[1]=e->irms;e->features[2]=e->peak;
            e->features[3]=e->irms>EPS?e->peak/e->irms:0;
            e->features[4]=e->vrms*e->irms>EPS?e->power/(e->vrms*e->irms):0;
            if(e->features[4]>1)e->features[4]=1;
            if(e->features[4]<-1)e->features[4]=-1;
            for(c=0;c<5;c++)e->query[c]=(e->features[c]-ni_mean[c])/ni_scale[c];
            e->index=0;e->stage=e->polarity_flipped?NI_POLARITY:NI_KNN;
        }
    }else if(e->stage==NI_POLARITY){
        end=e->index+128;if(end>NI_N)end=NI_N;
        for(;e->index<end;e->index++)e->current[e->index]=-e->current[e->index];
        if(e->index==NI_N){e->index=0;e->stage=NI_KNN;}
    }else if(e->stage==NI_KNN){
        if(e->extended){e->stage=NI_CONV1;e->index=0;e->cnn_calls++;e->cnn_status=NM_BUSY;return NM_BUSY;}
        end=e->index+32;if(end>NI_REF_ROWS)end=NI_REF_ROWS;
        for(;e->index<end;e->index++){
            double distance=0;
            for(c=0;c<5;c++){double delta=e->query[c]-ni_refs[e->index*5+c];distance+=delta*delta;}
            c=ni_labels[e->index];if(distance<e->class_distance[c])e->class_distance[c]=distance;
            if(distance<e->d1){e->d1=distance;e->knn_label=c;}
        }
        if(e->index==NI_REF_ROWS){
            double first=e->d1,second=DBL_MAX;
            for(c=0;c<NI_CLASSES;c++)if(c!=e->knn_label && e->class_distance[c]<second)second=e->class_distance[c];
            e->d1=sqrt(first);e->ratio=e->d1>EPS?sqrt(second)/e->d1:DBL_MAX;
            if(e->ratio<=1.7943130342282116 || (e->d1>=0.11907246048506813 && e->ratio<=3.0624601551994575)){
                e->route=NI_ROUTE_CNN_KEEP;e->stage=NI_CONV1;e->index=0;e->cnn_calls++;e->cnn_status=NM_BUSY;
            }else {e->route=NI_ROUTE_KNN;e->label=e->knn_label;return finish(e,r,0);}
        }
    }else if(e->stage==NI_CONV1)convolution(e,e->current,4096,1,8,9,ni_w1,ni_b1,ni_a1,ni_c1,NI_POOL1);
    else if(e->stage==NI_POOL1)pool(e,4096,8,NI_CONV2);
    else if(e->stage==NI_CONV2)convolution(e,e->b,2048,8,16,7,ni_w2,ni_b2,ni_a2,ni_c2,NI_POOL2);
    else if(e->stage==NI_POOL2)pool(e,2048,16,NI_CONV3);
    else if(e->stage==NI_CONV3){
        convolution(e,e->b,1024,16,32,5,ni_w3,ni_b3,ni_a3,ni_c3,NI_GLOBAL);
        if(e->stage==NI_GLOBAL)memset(e->embedding,0,sizeof(e->embedding));
    }else if(e->stage==NI_GLOBAL){
        end=e->index+128;if(end>32768)end=32768;
        for(;e->index<end;e->index++){
            c=e->index/1024;e->embedding[c]+=e->a[e->index]/1024.0f;
            if(e->a[e->index]>e->embedding[c+32])e->embedding[c+32]=e->a[e->index];
        }
        if(e->index==32768){e->index=0;e->stage=NI_FC;}
    }else if(e->stage==NI_FC){
        float best=-FLT_MAX;
        for(c=0;c<NI_CLASSES;c++){
            float value=ni_fc_b[c];for(n=0;n<64;n++)value+=e->embedding[n]*ni_fc_w[c*64+n];
            if(!isfinite(value)){ni_cancel(e);return NM_ERROR;}
            e->logits[c]=value;if(value>best){best=value;e->cnn_label=c;}
        }
        return decide(e,r);
    }else {ni_cancel(e);return NM_ERROR;}
    return NM_BUSY;
}
