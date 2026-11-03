/* PS event pipeline: 720-bin median background, no power fitting, CNN
 * embedding-conditioned VI lag, dynamic 14+N matching. nb_board owns causal
 * 3/6/9/12/15/18-window checkpoints and final publication. */
#include "nilm_event.h"
#include "nilm_assets.h"
#include "nilm_dynamic.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#define VG 0.2328271484375
#define IG 0.015556640625
static const char *states[]={"IDLE","BEFORE","ARMED","SETTLE","AFTER","PREP","PHASE","ALIGN","RESIDUAL","LAG","RECLASS","INFER","DONE","ERROR","CANCELLED"};
void ne_init(ne_event *e){memset(e,0,sizeof(*e));e->reason="NONE";e->engine.extended=2;}
int ne_busy(const ne_event *e){return e->state>=NE_BEFORE && e->state<=NE_INFER;}
void ne_fail(ne_event *e,const char *why){if(strcmp(why,"UNCONFIRMED_AT_18_WINDOWS"))e->last_failure=why;ni_cancel(&e->engine);e->state=NE_ERROR;e->reason=why;e->valid=0;}
void ne_push(ne_event *e,const nilm_stream_desc *d,const nilm_stream_sample *s,uint64_t now)
{
    if(!ne_busy(e))return;
    if(e->have_last && (d->epoch!=e->epoch || d->sequence!=e->last_seq+1 || d->first_id!=e->last_id+1u)){
        ne_fail(e,"INPUT_GAP");return;
    }
    e->have_last=1;e->epoch=d->epoch;e->last_seq=d->sequence;e->last_id=d->last_id;e->last_rx_ms=now;
    if(e->state==NE_SETTLE){if(++e->settle==8){e->state=NE_AFTER;e->blocks=0;}return;}
    if(e->state!=NE_BEFORE && e->state!=NE_AFTER)return;
    unsigned w=e->blocks/4+(e->state==NE_AFTER?3:0),offset=(e->blocks%4)*1024;
    if(!offset)e->first_id[w]=d->first_id;
    for(unsigned n=0;n<1024;n++){
        uint32_t data=s[n].data,v=(data>>16)&4095u,i=data&4095u;
        if(s[n].id!=d->first_id+n){ne_fail(e,"SAMPLE_ID");return;}
        if(v<=4 || v>=4091 || i<=4 || i>=4091){ne_fail(e,"ADC_RAIL");return;}
        e->raw[w][offset+n]=data;
    }
    e->blocks++;if(e->blocks%4==0)e->raw_ready[w]=1;
    if(e->blocks==12){
        if(e->state==NE_BEFORE)e->state=NE_ARMED;
        else {e->state=NE_PREP;e->window=e->index=e->prep_phase=0;e->mean_v=e->mean_i=0;}
    }
}
/* Reset only small result metadata, never the large waveform/workspace arrays. */
void ne_reset_results(ne_event *e){
    memset(e->ready,0,sizeof(e->ready));memset(e->residual_ready,0,sizeof(e->residual_ready));
    memset(e->registry,0,sizeof(e->registry));
    memset(e->recognition,0,sizeof(e->recognition));memset(e->labels,0,sizeof(e->labels));
    memset(e->routes,0,sizeof(e->routes));memset(e->knn,0,sizeof(e->knn));memset(e->cnn,0,sizeof(e->cnn));
    memset(e->support_score,0,sizeof(e->support_score));memset(e->candidate_score,0,sizeof(e->candidate_score));
    memset(e->support_match,0,sizeof(e->support_match));memset(e->support_calibrated,0,sizeof(e->support_calibrated));
    memset(e->embedding_score,0,sizeof(e->embedding_score));memset(e->auxiliary_score,0,sizeof(e->auxiliary_score));
    memset(e->auxiliary_probability,0,sizeof(e->auxiliary_probability));memset(e->auxiliary_bypassed,0,sizeof(e->auxiliary_bypassed));
    memset(e->reject_reason,0,sizeof(e->reject_reason));memset(e->confidence,0,sizeof(e->confidence));
    memset(e->corr,0,sizeof(e->corr));memset(e->rp,0,sizeof(e->rp));memset(e->ri,0,sizeof(e->ri));
    memset(e->power_error,0,sizeof(e->power_error));memset(e->shifts,0,sizeof(e->shifts));
    memset(e->prepared,0,sizeof(e->prepared));memset(&e->source,0,sizeof(e->source));
    e->votes=e->label=e->valid=0;
}
static int stable(ne_event *e,unsigned side){
    const double *x=e->power+side*3;
    double mean=(x[0]+x[1]+x[2])/3,ss=0;
    for(unsigned n=0;n<3;n++)ss+=(x[n]-mean)*(x[n]-mean);
    double deviation=sqrt(ss/3),limit=fmax(1.0,fabs(mean)*0.10);
    e->source.mean[side]=mean;e->source.stddev[side]=deviation;e->source.limit[side]=limit;
    int ok=isfinite(mean)&&isfinite(deviation)&&deviation<=limit;
    if(!ok)e->source.failed_mask|=1u<<side;
    return ok;
}
void ne_record_checkpoint(ne_event *e,unsigned checkpoint,const char *reason){
    e->source.checkpoint=checkpoint;e->source.reason=reason;
    memcpy(e->source.power,e->power,sizeof(e->power));
    memcpy(e->source.first_id,e->first_id,sizeof(e->first_id));
    if(e->checkpoint_count<6)e->checkpoints[e->checkpoint_count++]=e->source;
}
void ne_poll(ne_event *e,uint64_t now)
{
    if(!ne_busy(e))return;
    if(now-e->start_ms>60000){ne_fail(e,"TIMEOUT_60S");return;}
    if(now-e->last_rx_ms>2000){ne_fail(e,"INPUT_STALLED");return;}
    unsigned end=e->index+128;if(end>NI_N)end=NI_N;
    unsigned w=e->window,b=e->pair,a=b+3;
    if(e->state==NE_PREP){
        for(;e->index<end;e->index++){
            unsigned n=e->index;uint32_t d=e->raw[w][n];
            if(e->prep_phase==0){e->mean_v+=(d>>16)&4095u;e->mean_i+=d&4095u;}
            else if(e->prep_phase==1){
                double v=(((d>>16)&4095u)-e->mean_v)*VG,i=((d&4095u)-e->mean_i)*IG;
                e->v[w][n]=v;e->i[w][n]=i;e->vv[w]+=v*v;e->power[w]+=v*i;
            }else e->i[w][n]=-e->i[w][n];
        }
        if(e->index==NI_N){
            e->index=0;
            if(e->prep_phase==0){e->mean_v/=NI_N;e->mean_i/=NI_N;e->prep_phase=1;}
            else if(e->prep_phase==1 && e->power[w]<0){e->power[w]=-e->power[w];e->flipped[w]=1;e->prep_phase=2;}
            else {
                e->power[w]/=NI_N;e->prepared[w]=1;e->window++;e->prep_phase=0;e->mean_v=e->mean_i=0;
                if(e->window==6){
                    e->source.checked=1;int before_ok=stable(e,0),after_ok=stable(e,1);
                    if(!before_ok||!after_ok){ne_fail(e,"SOURCE_UNSTABLE");return;}
                    e->pair=0;nph_begin(&e->phase_job,e->v,e->i,e->residual,e->direction);e->state=NE_PHASE;
                }
            }
        }
    }else if(e->state==NE_PHASE){
        int status=nph_poll(&e->phase_job);
        if(status<0){ne_fail(e,"MAINS_PHASE_INVALID");return;}
        if(status){e->state=NE_RESIDUAL;e->index=0;e->ss=e->si=0;}
    }else if(e->state==NE_RESIDUAL){
        unsigned target=b+(e->direction?3:0);
        for(;e->index<end;e->index++){
            unsigned n=e->index;double i=e->residual[b][n];e->ss+=i*i;e->si+=e->v[target][n]*i;
        }
        if(e->index==NI_N){
            e->residual_ready[b]=1;e->rp[b]=fabs(e->si/NI_N);e->ri[b]=sqrt(e->ss/NI_N);
            e->drop=fabs(e->power[b]-e->power[a]);
            e->power_error[b]=fabs(e->rp[b]-e->drop)/fmax(fabs(e->drop),1.0);
            if(e->drop<1||e->rp[b]<1||e->ri[b]<.005){ne_fail(e,"NO_VALID_LOAD_CHANGE");return;}
            if(ni_submit_physical(&e->engine,e->v[target],e->residual[b],(uint64_t)e->id*3+b,&e->result)!=NM_BUSY){ne_fail(e,"MODEL_SUBMIT");return;}
            e->state=NE_INFER;
        }
    }else if(e->state==NE_INFER){
        int status=ni_poll(&e->engine,&e->result);
        if(status==NM_BUSY)return;
        if(status!=NM_DONE || e->engine.label>=ND_CLASSES){ne_fail(e,"MODEL_ERROR");return;}
        ng_begin(&e->lag_job,e->v[b+(e->direction?3:0)],e->engine.current,e->engine.embedding);e->state=NE_LAG;
    }else if(e->state==NE_LAG){
        if(!ng_poll(&e->lag_job))return;
        e->shifts[b]=e->lag_job.shift;e->corr[b]=e->lag_job.best;
        e->engine.features[0]=e->lag_job.power;
        e->engine.features[4]=e->engine.vrms*e->engine.irms>1e-12?fmin(1,e->lag_job.power/(e->engine.vrms*e->engine.irms)):0;
        for(unsigned n=0;n<5;n++)e->engine.query[n]=(e->engine.features[n]-ni_mean[n])/ni_scale[n];
        nd_begin(&e->engine.dynamic,e->engine.query,e->engine.embedding);e->state=NE_RECLASS;
    }else if(e->state==NE_RECLASS){
        if(!nd_poll(&e->engine.dynamic))return;
        e->engine.label=e->engine.dynamic.label;e->engine.knn_label=e->engine.dynamic.knn;e->engine.cnn_label=e->engine.dynamic.cnn;
        e->engine.route=e->engine.dynamic.called?(e->engine.label!=e->engine.knn_label?NI_ROUTE_CNN_OVERRIDE:NI_ROUTE_CNN_KEEP):NI_ROUTE_KNN;
        if(e->engine.dynamic.state==ND_KNOWN&&e->engine.label>=14)e->engine.route=NI_ROUTE_REGISTERED;
        e->labels[b]=e->engine.label;e->routes[b]=e->engine.route;
        memcpy(e->features[b],e->engine.query,sizeof(e->features[b]));
        memcpy(e->embeddings[b],e->engine.embedding,sizeof(e->embeddings[b]));
        e->recognition[b]=e->engine.dynamic.state;
        e->support_score[b]=e->engine.dynamic.support_min;e->candidate_score[b]=e->engine.dynamic.support5;
        e->support_match[b]=e->engine.dynamic.matched;e->support_calibrated[b]=e->engine.dynamic.support_calibrated;
        e->embedding_score[b]=e->engine.dynamic.open.distance_score;
        e->auxiliary_score[b]=e->engine.dynamic.open.auxiliary_score;
        e->auxiliary_probability[b]=e->engine.dynamic.open.probability;
        e->auxiliary_bypassed[b]=e->engine.dynamic.open.bypassed;
        e->reject_reason[b]=e->engine.dynamic.reason;
        const no_job *o=&e->engine.dynamic.open;
        e->registry[b]=(ne_registry_diag){o->registered_count,e->engine.dynamic.revision,o->registered_candidate,
            o->paired_base,o->paired_registered,o->registered_distance<1e100?o->candidate_score:1e300,o->registry_margin,o->registry_reason};
        e->knn[b]=e->engine.knn_label;e->cnn[b]=e->engine.cnn_label;
        e->confidence[b]=e->engine.dynamic.called?e->engine.dynamic.confidence:-1;e->ready[b]=1;
        if(++e->pair<3){e->state=NE_RESIDUAL;e->index=0;e->ss=e->si=0;return;}
        e->votes=0;
        for(unsigned n=0;n<3;n++){
            unsigned votes=0;for(unsigned j=0;j<3;j++)votes+=e->labels[n]==e->labels[j];
            if(votes>e->votes){e->votes=votes;e->label=e->labels[n];}
        }
        unsigned unknown=0;for(unsigned n=0;n<3;n++)unknown+=e->recognition[n]==ND_UNKNOWN;
        e->valid=e->votes>=2||unknown>=2;e->reason=e->valid?"PHASE_MEDIAN_CANDIDATE":"NO_MAJORITY";e->state=NE_DONE;
    }
}
const char *ne_identification(const ne_event *e){
    if(!e->id)return "idle";
    if(ne_busy(e))return "pending";
    if(e->state!=NE_DONE||!e->valid)return "uncertain";
    unsigned unknown=0,known=0;
    for(unsigned k=0;k<3;k++){unknown+=e->recognition[k]==ND_UNKNOWN;known+=e->recognition[k]==ND_KNOWN&&e->labels[k]==e->label;}
    return unknown==3?"unknown":known==3?"known":"uncertain";
}
const char *ne_public_label(const ne_event *e){
    const char *s=ne_identification(e);
    return !strcmp(s,"known")?nd_name(e->label):!strcmp(s,"unknown")?"UNKNOWN":"UNAVAILABLE";
}
static void append(char *out,size_t size,size_t *used,const char *fmt,...)
{
    if(*used>=size)return;
    va_list ap;va_start(ap,fmt);int n=vsnprintf(out+*used,size-*used,fmt,ap);va_end(ap);
    if(n<0){*used=size;return;}*used+=(size_t)n;
}
int ne_diagnostics_json(const ne_event *e,char *out,size_t size){
    size_t used=0;
    append(out,size,&used,"{\"event_id\":%u,\"direction\":\"%s\",\"last_failure\":\"%s\",\"stddev_absolute_limit_w\":1,\"stddev_relative_limit\":0.1,\"samples_per_window\":4096,\"nominal_window_ms\":160,\"checkpoints\":[",e->id,e->direction?"ON":"OFF",e->last_failure?e->last_failure:"NONE");
    for(unsigned k=0;k<e->checkpoint_count;k++){
        const ne_source_diag *d=&e->checkpoints[k];
        append(out,size,&used,"%s{\"checkpoint\":%u,\"after_windows_seen\":%u,\"reason\":\"%s\",\"source_checked\":%s,\"failed_side\":\"%s\",\"first_ids\":[",k?",":"",d->checkpoint,d->checkpoint*3,d->reason?d->reason:"NONE",d->checked?"true":"false",d->failed_mask==3?"BOTH":d->failed_mask==1?"BEFORE":d->failed_mask==2?"AFTER":"NONE");
        for(unsigned j=0;j<6;j++)append(out,size,&used,"%s%u",j?",":"",d->first_id[j]);
        append(out,size,&used,"],\"power_w\":[");
        for(unsigned j=0;j<6;j++){if(d->checked&&isfinite(d->power[j]))append(out,size,&used,"%s%.9g",j?",":"",d->power[j]);else append(out,size,&used,"%snull",j?",":"");}
        append(out,size,&used,"],\"sides\":[");
        for(unsigned j=0;j<2;j++){
            append(out,size,&used,"%s{\"side\":\"%s\",",j?",":"",j?"AFTER":"BEFORE");
            if(d->checked&&isfinite(d->mean[j])&&isfinite(d->stddev[j]))append(out,size,&used,"\"mean_w\":%.9g,\"stddev_w\":%.9g,\"limit_w\":%.9g,\"passed\":%s}",d->mean[j],d->stddev[j],d->limit[j],d->failed_mask&(1u<<j)?"false":"true");
            else append(out,size,&used,"\"mean_w\":null,\"stddev_w\":null,\"limit_w\":null,\"passed\":null}");
        }
        append(out,size,&used,"]}");
    }
    append(out,size,&used,"]}");return (int)used;
}
int ne_json(const ne_event *e,char *out,size_t size)
{
    size_t used=0;
    append(out,size,&used,"{\"protocol\":1,\"mode\":\"BOARD_PHASE_MEDIAN_ON_OFF_V1\",\"id\":%u,\"state\":\"%s\",\"identification_status\":\"%s\",\"reason\":\"%s\",\"valid\":%s,\"label\":\"%s\",\"votes\":%u,\"epoch\":%u,\"automatic\":true,\"hardware_validated\":false,\"raw_windows\":[",e->id,states[e->state],ne_identification(e),e->reason,e->valid?"true":"false",ne_public_label(e),e->votes,e->epoch);
    for(unsigned n=0;n<6;n++)append(out,size,&used,"%s{\"ready\":%s,\"first_id\":%u,\"polarity_flipped\":%s}",n?",":"",e->raw_ready[n]?"true":"false",e->first_id[n],e->flipped[n]?"true":"false");
    append(out,size,&used,"],\"pairs\":[");
    for(unsigned n=0;n<3;n++){
        char conf[32];if(e->ready[n] && e->confidence[n]>=0)snprintf(conf,sizeof(conf),"%.9g",e->confidence[n]);else strcpy(conf,"null");
        append(out,size,&used,"%s{\"ready\":%s,\"residual_ready\":%s,\"label\":\"%s\",\"route\":%u,\"knn_label\":\"%s\",\"cnn_label\":\"%s\",\"cnn_confidence\":%s,\"shift\":%d,\"correlation\":%.9g,\"power_w\":%.9g,\"irms_a\":%.9g,\"power_error\":%.9g}",n?",":"",e->ready[n]?"true":"false",e->residual_ready[n]?"true":"false",e->ready[n]?nd_name(e->labels[n]):"UNAVAILABLE",e->routes[n],e->ready[n]&&e->knn[n]<NI_CLASSES?nd_name(e->knn[n]):"UNAVAILABLE",e->ready[n]&&e->cnn[n]<NI_CLASSES?nd_name(e->cnn[n]):"NOT_CALLED",conf,e->shifts[n],e->corr[n],e->rp[n],e->ri[n],e->power_error[n]);
    }
    append(out,size,&used,"],\"arm_ms\":%llu,\"mark_ms\":%llu,\"mark_after_sample_id\":%u}\n",(unsigned long long)e->arm_ms,(unsigned long long)e->mark_ms,e->mark_last_id);return (int)used;
}
int ne_command(ne_event *e,const char *cmd,char *out,size_t size,uint64_t now,int enabled)
{
    char verb[16],extra;unsigned id,w,offset;int n;
    if(!strcmp(cmd,"STATUS"))return ne_json(e,out,size);
    if(sscanf(cmd,"READ %u %15s %u %u %c",&id,verb,&w,&offset,&extra)==4){
        if(id!=e->id || ne_busy(e) || !id || offset>=NI_N || offset%64)goto invalid;
        int raw=!strcmp(verb,"RAW"),res=!strcmp(verb,"RES");
        if((!raw&&!res) || (raw&&(w>=6||!e->raw_ready[w])) || (res&&(w>=3||!e->residual_ready[w])))goto invalid;
        size_t used=0;append(out,size,&used,"{\"id\":%u,\"offset\":%u,\"data\":[",id,offset);
        for(unsigned j=offset;j<offset+64;j++){
            if(raw)append(out,size,&used,"%s%u",j==offset?"":",",e->raw[w][j]);
            else append(out,size,&used,"%s[%.17g,%.17g]",j==offset?"":",",e->v[w][j],e->residual[w][j]);
        }
        append(out,size,&used,"]}\n");return (int)used;
    }
    if(sscanf(cmd,"%15s %u %c",verb,&id,&extra)!=2 || !id)goto invalid;
    if(!strcmp(verb,"ARM")){
        if(id==e->id)return ne_json(e,out,size); /* transport retries cannot restart a capture */
        if(!enabled)return snprintf(out,size,"{\"error\":\"NOT_READY\"}\n");
        if(ne_busy(e))return snprintf(out,size,"{\"error\":\"BUSY\"}\n");
        /* Small metadata reset only: don't block acquisition clearing all arrays. */
        e->state=NE_BEFORE;e->id=id;e->blocks=e->settle=e->have_last=e->pair=e->valid=e->votes=0;
        ne_reset_results(e);e->checkpoint_count=0;e->last_failure="NONE";memset(e->checkpoints,0,sizeof(e->checkpoints));
        e->reason="NONE";e->start_ms=e->last_rx_ms=now;
        e->arm_ms=now;e->mark_ms=0;e->mark_last_id=0;e->epoch=e->last_id=0;e->last_seq=0;
        memset(e->raw_ready,0,sizeof(e->raw_ready));memset(e->prepared,0,sizeof(e->prepared));
        memset(e->flipped,0,sizeof(e->flipped));memset(e->first_id,0,sizeof(e->first_id));
        memset(e->vv,0,sizeof(e->vv));memset(e->power,0,sizeof(e->power));
        memset(e->ready,0,sizeof(e->ready));memset(e->residual_ready,0,sizeof(e->residual_ready));
        memset(e->corr,0,sizeof(e->corr));memset(e->rp,0,sizeof(e->rp));memset(e->ri,0,sizeof(e->ri));
        memset(e->power_error,0,sizeof(e->power_error));memset(e->routes,0,sizeof(e->routes));memset(e->shifts,0,sizeof(e->shifts));
        ni_cancel(&e->engine);
    }else if(!strcmp(verb,"MARK")){
        if(id!=e->id)goto invalid;
        if(e->state==NE_ARMED){e->state=NE_SETTLE;e->settle=0;e->start_ms=now;e->mark_ms=now;e->mark_last_id=e->last_id;}
        else if(e->state<NE_SETTLE || e->state>NE_DONE)goto invalid;
    }else if(!strcmp(verb,"CANCEL")){
        if(id!=e->id)goto invalid;
        if(ne_busy(e)){ni_cancel(&e->engine);e->valid=0;e->state=NE_CANCELLED;e->reason="USER_CANCELLED";}
    }else goto invalid;
    n=ne_json(e,out,size);return n;
invalid:
    return snprintf(out,size,"{\"error\":\"INVALID_COMMAND_OR_STATE\"}\n");
}
