/* Regressions for real-board cold-boot HairDryer / unassigned-energy report. */
#include "nilm_dorm.h"
#include "nilm_dynamic.h"
#include "nilm_store.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static nb_board b;
static char json[40000];
static uint64_t now;
static unsigned eid;
static void snapshot(void){int n=dorm_json(&b,json,sizeof(json),now);assert(n>0&&n<(int)sizeof(json));}
static int has(const char *s){snapshot();return strstr(json,s)!=0;}
static double field(const char *s){snapshot();const char *p=strstr(json,s);assert(p);return strtod(p+strlen(s),0);}
static void block(double p,double i){
    now+=40;b.power=p;b.irms=i;b.vrms=235;b.power_valid=1;b.last_input_ms=now;
    dorm_block(&b,now);dorm_poll(&b,now);
}
static void reset(void){ns_init();nb_init(&b);dorm_init();now=100;eid=0;}
static void event(unsigned label,unsigned on,unsigned state,double before,double after,double rms){
    ne_init(&b.event);ne_event *e=&b.event;e->id=++eid;e->state=NE_DONE;e->valid=1;
    e->label=label;e->direction=on;e->mark_ms=now;
    for(unsigned k=0;k<3;k++){
        e->recognition[k]=state;e->labels[k]=label;e->rp[k]=fabs(after-before);
        e->power[k]=before;e->power[k+3]=after;e->prepared[k]=e->prepared[k+3]=1;e->vv[k]=235.*235*NI_N;
        for(unsigned n=0;n<NI_N;n++)e->i[k][n]=(n&1)?rms:-rms;
    }
    dorm_poll(&b,now);
}
static void cold_start(void){
    reset();for(unsigned k=0;k<25;k++)block(.7,.034);
    assert(has("\"off\":false"));assert(has("\"tracking_uncertain\":true"));
    event(7,1,ND_KNOWN,.7,340.7,.034);
}
int main(void){
    cold_start();
    for(unsigned k=0;k<27;k++)block(340.7,1.45);
    assert(has("\"tracking_uncertain\":true"));assert(has("\"power_w\":0,"));
    double past=field("\"unassigned_wh\":");block(340.7,1.45);
    assert(has("\"tracking_uncertain\":false"));assert(has("TRACKING_RECOVERED"));
    assert(has("KNOWN_ON_POWER_BALANCE"));assert(has("\"power_w\":340.7,"));
    assert(fabs(field("\"unassigned_wh\":")-past)<1e-8);
    double wh=field("\"energy_wh\":");assert(wh>0);
    for(unsigned k=0;k<25;k++)block(340.7,1.45);
    assert(fabs(field("\"energy_wh\":")-wh-340.7/3600)<1e-8);
    event(7,0,ND_KNOWN,340.7,.7,1.45);block(.7,.034);
    assert(has("\"on\":false"));assert(has("\"power_w\":0,"));
    double retained=field("\"energy_wh\":");block(.7,.034);assert(field("\"energy_wh\":")==retained);
    /* A quiet power reading alone is not an all-off assertion. */
    reset();for(unsigned k=0;k<100;k++)block(.7,.034);assert(has("\"tracking_uncertain\":true"));
    /* Unexplained residual power, reactive baseline, ambiguous/unknown labels. */
    cold_start();for(unsigned k=0;k<80;k++)block(400,1.8);assert(has("\"tracking_uncertain\":true"));
    reset();block(.7,.12);event(7,1,ND_KNOWN,.7,340.7,.12);
    for(unsigned k=0;k<30;k++)block(340.7,1.45);assert(has("\"tracking_uncertain\":true"));
    reset();block(20,.1);event(7,1,ND_KNOWN,20,360,.1);
    for(unsigned k=0;k<30;k++)block(360,1.6);assert(has("\"tracking_uncertain\":true"));
    for(unsigned state=ND_UNKNOWN;state<=ND_AMBIGUOUS;state++){
        reset();block(.7,.034);event(7,1,state,.7,340.7,.034);
        for(unsigned k=0;k<30;k++)block(340.7,1.45);assert(has("\"tracking_uncertain\":true"));
    }
    /* Quality loss and missed overlapping events cancel pending recovery. */
    cold_start();for(unsigned k=0;k<10;k++)block(340.7,1.45);
    b.gaps++;block(340.7,1.45);for(unsigned k=0;k<30;k++)block(340.7,1.45);
    assert(has("\"tracking_uncertain\":true"));
    cold_start();b.busy_drops++;block(340.7,1.45);
    for(unsigned k=0;k<30;k++)block(340.7,1.45);assert(has("\"tracking_uncertain\":true"));
    cold_start();for(unsigned k=0;k<28;k++)block(340.7,1.45);
    event(7,1,ND_KNOWN,.7,340.7,.034);assert(has("REPEATED_CLASS"));
    for(unsigned k=0;k<30;k++)block(340.7,1.45);assert(has("\"tracking_uncertain\":true"));
    /* Gap resets the consecutive OFF gate; a stale partial count cannot clear it. */
    reset();for(unsigned k=0;k<3;k++)block(0,0);b.gaps++;block(0,0);block(0,0);
    assert(has("\"tracking_uncertain\":true"));
    for(unsigned k=0;k<3;k++)block(0,0);assert(has("\"tracking_uncertain\":false"));
    puts("PASS accounting-r1: noisy-baseline known ON recovery, prospective energy, OFF retention, power mismatch, reactive/unexplained load, unknown/ambiguous, gaps, busy drops, duplicates and consecutive OFF gate");
}
