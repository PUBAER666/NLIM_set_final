#include "nilm_open.h"
#include "nilm_open_assets.h"
#include "nilm_dynamic.h"
#include "nilm_store.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static no_job j;
static no_reference refs[2];
static double q[5];static float e[64],logits[14];
static void vector(unsigned k){
    memcpy(q,no_golden_x+k*5,sizeof(q));
    for(unsigned n=0;n<64;n++)e[n]=(float)no_golden_e[k*64+n];
    for(unsigned n=0;n<14;n++)logits[n]=(float)no_golden_logits[k*14+n];
}
static void run(unsigned count){
    no_begin(&j,q,e,logits,refs,count);unsigned guard=0;
    while(!no_poll(&j))assert(++guard<1000);
}
int main(void){
    assert(no_selftest());vector(1);run(0);assert(j.state==NO_UNKNOWN);
    double base=j.paired_base;
    refs[0].id=14;memcpy(refs[0].x,q,sizeof(q));memcpy(refs[0].embedding,e,sizeof(e));
    /* A base-UNKNOWN result no longer vetoes registered support solely because
       a rejected base reference has a smaller paired distance. */
    refs[0].x[0]+=sqrt(5*(base+1));run(1);
    assert(j.paired_registered>j.paired_base&&j.state==NO_KNOWN&&j.label==14&&j.bypassed);
    assert(!strcmp(j.registry_reason,"REGISTERED_ACCEPTED"));
    memcpy(refs[0].x,q,sizeof(q));refs[1]=refs[0];run(2);
    assert(j.state==NO_KNOWN); /* references of the same name are not rivals */
    refs[1].id=15;run(2);
    assert(j.state==NO_UNKNOWN&&!j.bypassed&&j.score>1);
    assert(!strcmp(j.registry_reason,"REGISTERED_CLASS_AMBIGUOUS"));
    /* Absolute support stays mandatory even with a single registered class. */
    refs[0].embedding[0]+=(float)(2*no_registered_radius/no_multiplier/no_diag[0]);
    run(1);assert(j.state==NO_UNKNOWN&&!j.bypassed&&j.candidate_score>1);
    assert(!strcmp(j.registry_reason,"REGISTERED_OUTSIDE_RADIUS"));
    vector(0);memcpy(refs[0].x,q,sizeof(q));memcpy(refs[0].embedding,e,sizeof(e));
    run(1);assert(j.state==NO_KNOWN&&j.label<14&&!j.bypassed);
    assert(!strcmp(j.registry_reason,"BASE_KNOWN_PROTECTED"));
    /* Removing a profile invalidates an in-flight job and rebuilds cached refs. */
    ns_init();vector(1);ns_pending=ns_active;
    assert(nl_enroll(&ns_pending,"registered_test",1,q,e)>0);assert(ns_begin()==0);ns_poll();
    nd_job job;nd_begin(&job,q,e);while(!nd_poll(&job)){}
    assert(job.state==ND_KNOWN&&job.label==14);
    nd_begin(&job,q,e);ns_pending=ns_active;assert(nl_clear(&ns_pending)==0);assert(ns_begin()==0);ns_poll();
    assert(nd_poll(&job)&&job.state==ND_INVALID&&!strcmp(job.reason,"REFERENCE_SET_CHANGED"));
    nd_begin(&job,q,e);while(!nd_poll(&job)){}
    assert(job.state==ND_UNKNOWN&&!job.open.registered_count&&!job.open.bypassed);
    puts("PASS registered support without base-distance veto, distinct-class ambiguity, absolute radius, base known protection, delete invalidates jobs and caches");
}
