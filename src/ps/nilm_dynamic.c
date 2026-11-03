/* Frozen 64D + forest on PS; convolution/features remain in PL. */
#include "nilm_dynamic.h"
#include "nilm_assets.h"
#include "nilm_store.h"
#include <string.h>
static no_reference cache[NL_PROFILES*NL_EVENTS];
static unsigned cache_revision,cache_count,cache_valid,cache_boot,cache_profiles;
const char *nd_name(unsigned c){return c<14?ni_classes[c]:c<14+ns_active.count?ns_active.profiles[c-14].name:"UNAVAILABLE";}
void nd_begin(nd_job *j,const double *q,const float *e){
    memset(j,0,sizeof(*j));j->revision=ns_active.revision;j->reason="PENDING";
    if(!cache_valid||cache_revision!=ns_active.revision||cache_boot!=ns_active.boot_serial||cache_profiles!=ns_active.count){
        cache_count=0;
        for(unsigned c=0;c<ns_active.count;c++)for(unsigned k=0;k<ns_active.profiles[c].count;k++){
            no_reference *r=&cache[cache_count++];const nl_example *x=&ns_active.profiles[c].examples[k];
            r->id=14+c;memcpy(r->x,x->vectors,sizeof(r->x));memcpy(r->embedding,x->embedding,sizeof(r->embedding));
        }
        cache_revision=ns_active.revision;cache_boot=ns_active.boot_serial;cache_profiles=ns_active.count;cache_valid=1;
    }
    float logits[14];
    for(unsigned c=0;c<14;c++){float v=ni_fc_b[c];for(unsigned n=0;n<64;n++)v+=e[n]*ni_fc_w[c*64+n];logits[c]=v;}
    no_begin(&j->open,q,e,logits,cache,cache_count);
}
int nd_poll(nd_job *j){
    if(j->phase==4)return 1;
    if(j->revision!=ns_active.revision){j->state=ND_INVALID;j->reason="REFERENCE_SET_CHANGED";j->phase=4;return 1;}
    if(!no_poll(&j->open))return 0;
    const no_job *o=&j->open;
    j->label=o->label;j->knn=o->knn;j->cnn=o->cnn;j->called=o->called;j->confidence=o->confidence;
    j->d1=o->d1;j->d2=o->d2;j->support_min=o->score;j->support5=o->candidate_score;
    j->support64=o->distance_score;j->matched=o->matched;j->support_calibrated=o->label<14&&o->matched<14;
    j->state=o->state==NO_KNOWN?ND_KNOWN:o->state==NO_UNKNOWN?ND_UNKNOWN:ND_INVALID;j->reason=o->reason;
    if(!ns_active.unknown_enabled&&j->state==ND_UNKNOWN){j->label=o->closed_label;j->state=ND_KNOWN;j->reason="REJECTION_DISABLED";}
    j->phase=4;return 1;
}
