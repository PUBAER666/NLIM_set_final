#include "nilm_infer.h"
#include "nilm_store.h"
#include <string.h>
static ni_engine engine;
void init_integration(void){ns_init();}
int run_physical(const double *v,const double *i,double *features,float *embedding,double *out){
    memset(&engine,0,sizeof(engine));engine.extended=1;nm_result result;
    int status=ni_submit_physical(&engine,v,i,1,&result);unsigned guard=0;
    while(status==NM_BUSY&&guard++<100000)status=ni_poll(&engine,&result);
    if(status!=NM_DONE)return -1;
    memcpy(features,engine.features,5*sizeof(double));memcpy(embedding,engine.embedding,64*sizeof(float));
    nd_job *j=&engine.dynamic;
    out[0]=j->state;out[1]=j->label;out[2]=j->support_min;out[3]=j->open.probability;
    out[4]=j->open.bypassed;out[5]=guard;return 0;
}
int register_integration(const double *q,const float *e,unsigned key){
    ns_pending=ns_active;int n=nl_enroll(&ns_pending,"Scope_Integration",((uint64_t)ns_active.boot_serial<<32)|key,q,e);
    if(n<0)return n;if(ns_begin())return -20;ns_poll();return n;
}
