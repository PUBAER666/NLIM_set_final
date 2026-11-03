/* All processing runs on PS. No PC clock, network connection or UI is required.
 * Time/energy use nominal 25600 samples/s, not calibrated physical time. */
#include "nilm_board.h"
#include "nilm_assets.h"
#include "nilm_dynamic.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CONFIRM_MAX_BLOCKS 32u
static void note(nb_board *b,const char *reason,uint64_t now){
    nb_trace *t=&b->trace[b->trace_next];
    t->ms=now;t->sequence=b->previous.sequence;t->phase=b->phase;t->reason=reason;
    t->power=b->power;t->baseline=b->baseline;t->threshold=b->threshold;
    b->trace_next=(b->trace_next+1)%16;if(b->trace_count<16)b->trace_count++;
}
static void reset_detector(nb_board *b) {
    b->hcount=b->hnext=b->pcount=b->cooldown=b->unstable=b->confirm_blocks=0;b->phase=NB_WARMUP;
    b->stable_valid=b->background_locked=b->background_valid=b->quiet_blocks=0;
}
void nb_init(nb_board *b){memset(b,0,sizeof(*b));ne_init(&b->event);b->enabled=1;}
int nb_set_factor(nb_board *b,double factor) {
    if(!isfinite(factor)||factor<0||factor>5)return -1;
    // Carbon is prospective: energy before the factor was set is not backfilled.
    b->factor=factor;b->factor_set=1;return 0;
}
static int metrics(const nilm_stream_desc *d,const nilm_stream_sample *s,nb_metric *m) {
    double sv=0,si=0,vv=0,vi=0,ii=0;
    if(d->last_id!=d->first_id+1023u)return 0;
    for(unsigned n=0;n<1024;n++) {
        unsigned v=(s[n].data>>16)&4095u,i=s[n].data&4095u;
        if(s[n].id!=d->first_id+n || (s[n].data&0xf000f000u) || v<=4||v>=4091||i<=4||i>=4091)return 0;
        sv+=v;si+=i;
    }
    sv/=1024;si/=1024;
    for(unsigned n=0;n<1024;n++) {
        double v=(((s[n].data>>16)&4095u)-sv)*0.2328271484375;
        double i=((s[n].data&4095u)-si)*0.015556640625;
        vv+=v*v;vi+=v*i;ii+=i*i;
    }
    m->p=fabs(vi/1024);m->v=sqrt(vv/1024);m->i=sqrt(ii/1024);return 1;
}
static void stats(const nb_metric *a,unsigned n,double *p,double *v,double *ps,double *vs) {
    *p=*v=*ps=*vs=0;
    for(unsigned k=0;k<n;k++){*p+=a[k].p;*v+=a[k].v;}*p/=n;*v/=n;
    for(unsigned k=0;k<n;k++){*ps+=(a[k].p-*p)*(a[k].p-*p);*vs+=(a[k].v-*v)*(a[k].v-*v);}
    *ps=sqrt(*ps/n);*vs=sqrt(*vs/n);
}
static void history(nb_board *b,nb_metric m){b->history[b->hnext]=m;b->hnext=(b->hnext+1)%25;if(b->hcount<25)b->hcount++;}
static int arm(nb_board *b) {
    double p,v,ps,vs;if(b->hcount<25)return 0;
    stats(b->history,25,&p,&v,&ps,&vs);
    if(v<1||ps>fmax(1,p*.02)||vs>v*.03)return 0;
    if(!b->background_locked){b->stable_sequence=b->previous.sequence;b->stable_valid=1;}
    b->baseline=p;b->baseline_v=v;b->threshold=fmax(5,fmax(p*.05,6*ps));b->phase=NB_ARMED;b->unstable=0;return 1;
}
static int snapshot(nb_board *b,uint64_t seq,unsigned window,unsigned offset) {
    unsigned slot=(unsigned)(seq%64);
    if(b->desc[slot].epoch!=b->previous.epoch || b->desc[slot].sequence!=seq)return 0;
    memcpy(b->event.raw[window]+offset,b->raw[slot],4096);
    if(!offset)b->event.first_id[window]=b->desc[slot].first_id;
    if(offset==3072)b->event.raw_ready[window]=1;
    return 1;
}
static void lock_background(nb_board *b,uint64_t now){
    if(b->background_locked)return;
    b->background_locked=1;b->background_ms=now;b->quiet_blocks=0;b->background_valid=0;
    if(!b->stable_valid||b->stable_sequence<13)return;
    uint64_t first=b->stable_sequence-13;
    for(unsigned n=0;n<12;n++){
        unsigned slot=(unsigned)((first+n)%64);
        if(b->desc[slot].epoch!=b->previous.epoch||b->desc[slot].sequence!=first+n)return;
        memcpy(b->background[n/4]+(n%4)*1024,b->raw[slot],4096);
        if(!(n%4))b->background_ids[n/4]=b->desc[slot].first_id;
    }
    b->background_valid=1;
}
void nb_registry_changed(nb_board *b){
    ne_fail(&b->event,"REGISTRY_CHANGED");ne_reset_results(&b->event);
    b->collecting=b->checkpoint=0;reset_detector(b);
}
static void collect_post(nb_board *b){
    while(b->post_copied<12&&b->post_first+b->post_copied<=b->previous.sequence){
        unsigned n=b->post_copied;
        if(!snapshot(b,b->post_first+n,3+n/4,(n%4)*1024)){ne_fail(&b->event,"AFTER_NOT_AVAILABLE");b->collecting=0;b->checkpoint=0;return;}
        b->post_copied++;
    }
    if(b->post_copied==12){
        ne_event *e=&b->event;b->collecting=0;e->state=NE_PREP;e->window=e->index=e->prep_phase=0;e->mean_v=e->mean_i=0;e->valid=0;
        memset(e->vv,0,sizeof(e->vv));memset(e->power,0,sizeof(e->power));memset(e->flipped,0,sizeof(e->flipped));
        memset(e->ready,0,sizeof(e->ready));memset(e->residual_ready,0,sizeof(e->residual_ready));
    }
}
static void start_event(nb_board *b,uint64_t now) {
    if(ne_busy(&b->event)||b->collecting){b->busy_drops++;ne_fail(&b->event,"OVERLAPPING_EVENTS");b->collecting=b->checkpoint=0;return;}
    char cmd[48],reply[8192];
    snprintf(cmd,sizeof(cmd),"ARM %u",b->event_count);
    ne_command(&b->event,cmd,reply,sizeof(reply),now,1);
    b->event.epoch=b->previous.epoch;b->event.direction=b->direction;
    b->event.mark_ms=b->trigger_ms;b->event.mark_last_id=b->trigger_first_id;
    if(!b->background_valid){ne_fail(&b->event,"BEFORE_NOT_AVAILABLE");return;}
    if(now-b->background_ms>8000){ne_fail(&b->event,"BACKGROUND_EXPIRED");return;}
    for(unsigned w=0;w<3;w++){
        memcpy(b->event.raw[w],b->background[w],sizeof(b->background[w]));
        b->event.first_id[w]=b->background_ids[w];b->event.raw_ready[w]=1;
    }
    /* Start AFTER confirmation, not at the earlier transient's fixed offset. */
    b->checkpoint=1;b->previous_agree=0;b->post_first=b->confirm+1;b->post_copied=0;
    b->background_locked=b->background_valid=b->stable_valid=0;
    b->event.state=NE_AFTER;b->collecting=1;collect_post(b);
}
void nb_push(nb_board *b,const nilm_stream_desc *d,const nilm_stream_sample *s,uint64_t now) {
    int gap=b->have_last && (d->epoch!=b->previous.epoch||d->sequence!=b->previous.sequence+1||d->first_id!=b->previous.last_id+1u);
    if(gap) {
        b->gaps++;reset_detector(b);
        note(b,"INPUT_GAP",now);
        if(b->collecting){ne_fail(&b->event,"INPUT_GAP");b->collecting=0;}
    }
    b->previous=*d;b->have_last=1;
    nb_metric m;
    if(!metrics(d,s,&m)) {
        b->bad_blocks++;b->power_valid=0;reset_detector(b);
        note(b,"INVALID_CAPTURE",now);
        if(b->collecting){ne_fail(&b->event,"INVALID_CAPTURE");b->collecting=0;}return;
    }
    unsigned slot=(unsigned)(d->sequence%64);b->desc[slot]=*d;
    for(unsigned n=0;n<1024;n++)b->raw[slot][n]=s[n].data;
    b->power=m.p;b->vrms=m.v;b->irms=m.i;b->power_valid=1;b->valid_blocks++;
    b->last_input_ms=now;
    double wh=m.p*(1024.0/25600.0)/3600.0;
    b->energy_wh+=wh;if(b->factor_set)b->carbon_kg+=wh*.001*b->factor;
    if(ne_busy(&b->event))b->event.last_rx_ms=now;
    if(b->collecting)collect_post(b);
    if(!b->enabled)return;
    if(b->phase==NB_COOLDOWN){history(b,m);if(--b->cooldown==0){b->phase=NB_WARMUP;if(arm(b))note(b,"REARMED",now);}return;}
    if(b->phase==NB_WARMUP){history(b,m);if(arm(b))note(b,"BASELINE_READY",now);return;}
    if(fabs(m.v-b->baseline_v)>b->baseline_v*.10){reset_detector(b);history(b,m);note(b,"VOLTAGE_CHANGE_RESET",now);return;}
    int dropped=fabs(b->baseline-m.p)>=b->threshold;
    if(b->phase==NB_CONFIRMING && ((m.p>b->baseline)?1u:0u)!=b->direction)dropped=0;
    if(b->background_locked&&now-b->background_ms>8000){
        reset_detector(b);history(b,m);note(b,"BACKGROUND_EXPIRED",now);return;
    }
    if(b->phase==NB_ARMED) {
        if(dropped) {
            lock_background(b,now);
            b->direction=m.p>b->baseline;
            b->phase=NB_CONFIRMING;b->pcount=1;b->confirm_blocks=1;b->pending[0]=m;
            // Do not overwrite the active event's sequence while it is collecting.
            // Cooldown (25 blocks) exceeds the 12 post-confirm capture blocks.
            b->trigger=d->sequence;b->trigger_ms=now;b->trigger_first_id=d->first_id;
            note(b,b->direction?"ON_TRIGGER":"OFF_TRIGGER",now);
        } else {
            if(b->background_locked){
                if(fabs(m.p-b->baseline)<=fmax(1,b->baseline*.02))b->quiet_blocks++;else b->quiet_blocks=0;
                if(b->quiet_blocks>=25){b->background_locked=b->background_valid=0;b->quiet_blocks=0;}
                else return; /* Do not learn a staged startup into the background. */
            }
            history(b,m);if(!arm(b)&&++b->unstable>=25){reset_detector(b);history(b,m);note(b,"BASELINE_UNSTABLE_RESET",now);}
        }
        return;
    }
    if(!dropped){
        /* A transient returned to the old baseline. Keep that baseline rather
         * than becoming blind for another full warmup interval. */
        b->confirm_cancelled++;b->pcount=b->confirm_blocks=0;b->phase=NB_ARMED;
        note(b,"CHANGE_RETURNED_TO_BASELINE",now);return;
    }
    b->confirm_blocks++;
    if(b->pcount==8){memmove(b->pending,b->pending+1,7*sizeof(b->pending[0]));b->pcount=7;}
    b->pending[b->pcount++]=m;if(b->pcount<8)return;
    double p,v,ps,vs;stats(b->pending+3,5,&p,&v,&ps,&vs);
    if(ps>fmax(1,p*.04)){
        b->confirm_retries++;
        /* Preserve the original before-trigger waveform for staged startup.
         * Bound waiting so all pre-event blocks still exist in the 64-bank ring:
         * 14 pre blocks + at most 32 confirmation blocks < 64. */
        if(b->confirm_blocks>=CONFIRM_MAX_BLOCKS){
            b->confirm_timeouts++;reset_detector(b);history(b,m);note(b,"CONFIRM_TIMEOUT",now);
        }else if(b->confirm_blocks==8)note(b,"WAIT_FOR_STABLE_CHANGE",now);
        return;
    }
    b->event_count++;b->confirm=d->sequence;start_event(b,now);
    note(b,ne_busy(&b->event)?"CHANGE_CONFIRMED":"EVENT_START_FAILED",now);
    b->hcount=b->hnext=0;for(unsigned n=0;n<8;n++)history(b,b->pending[n]);
    b->pcount=b->confirm_blocks=0;b->phase=NB_COOLDOWN;b->cooldown=25;
}
void nb_poll(nb_board *b,uint64_t now){
    if(now-b->last_input_ms>2000)b->power_valid=0;
    ne_poll(&b->event,now);
    ne_event *e=&b->event;
    int retryable=e->state==NE_DONE || (e->state==NE_ERROR&&(!strcmp(e->reason,"SOURCE_UNSTABLE")||!strcmp(e->reason,"NO_VALID_LOAD_CHANGE")));
    if(b->checkpoint&&retryable){
        unsigned unknown=0;for(unsigned k=0;k<3;k++)unknown+=e->recognition[k]==ND_UNKNOWN;
        unsigned signature=unknown==3?ND_CLASSES+1:e->label+1;
        unsigned agree=e->state==NE_DONE&&e->valid&&(unknown==3||(e->votes==3&&e->recognition[0]==ND_KNOWN&&e->recognition[1]==ND_KNOWN&&e->recognition[2]==ND_KNOWN));
        const char *attempt=e->reason;
        if(e->state==NE_DONE&&!agree)attempt="WINDOW_DECISIONS_DISAGREE";
        else if(agree&&b->previous_agree&&signature!=b->previous_signature)attempt="CHECKPOINT_DECISIONS_DISAGREE";
        if(!agree||!strcmp(attempt,"CHECKPOINT_DECISIONS_DISAGREE"))e->last_failure=attempt;
        ne_record_checkpoint(e,b->checkpoint,attempt);
        if(e->source.checked&&(e->source.failed_mask&1)){ne_fail(e,"BACKGROUND_UNSTABLE");b->checkpoint=0;}
        else if(agree&&b->previous_agree&&signature==b->previous_signature){e->reason="PHASE_FINAL_TWO_CHECKPOINTS";b->checkpoint=0;}
        else if(b->checkpoint>=6){if(!e->last_failure||!strcmp(e->last_failure,"NONE"))e->last_failure="INSUFFICIENT_AGREEING_CHECKPOINTS";ne_fail(e,"UNCONFIRMED_AT_18_WINDOWS");b->checkpoint=0;}
        else {
            b->previous_signature=signature;b->previous_agree=agree;b->checkpoint++;
            ne_reset_results(e);memset(e->raw_ready+3,0,3*sizeof(e->raw_ready[0]));
            b->post_first=b->confirm+1+12*(b->checkpoint-1);b->post_copied=0;b->collecting=1;e->state=NE_AFTER;e->valid=0;
            collect_post(b);
        }
    }
    if(b->collecting && b->event.state!=NE_AFTER)b->collecting=0;
}
int nb_json(const nb_board *b,char *out,size_t size) {
    char power[48],carbon[48],factor[48];
    if(b->power_valid)snprintf(power,sizeof(power),"%.12g",b->power);else strcpy(power,"null");
    if(b->factor_set){snprintf(carbon,sizeof(carbon),"%.12g",b->carbon_kg);snprintf(factor,sizeof(factor),"%.12g",b->factor);}
    else {strcpy(carbon,"null");strcpy(factor,"null");}
    int n=snprintf(out,size,"{\"compute_location\":\"BOARD_PS\",\"power_w\":%s,\"energy_wh\":%.12g,\"carbon_kg\":%s,\"factor\":%s,"
      "\"time_basis\":\"NOMINAL_25600_HZ\",\"metrology_verified\":false,\"valid_blocks\":%llu,\"bad_blocks\":%llu,\"gaps\":%llu,"
      "\"automatic_events\":true,\"phase\":%u,\"events\":%u,\"busy_drops\":%u,\"event_id\":%u,\"event_state\":%u,"
      "\"event_valid\":%s,\"label\":\"%s\",\"event_reason\":\"%s\",\"energy_scope\":\"CURRENT_BOOT_VALID_BLOCKS\","
      "\"detector\":{\"revision\":\"event-r3-frozen-background\",\"voltage_rms_v\":%.12g,\"current_rms_a\":%.12g,\"baseline_w\":%.12g,\"threshold_w\":%.12g,"
      "\"confirm_blocks\":%u,\"confirm_limit_blocks\":%u,\"unstable_retries\":%u,\"cancelled\":%u,\"timeouts\":%u,\"trace\":[",
      power,b->energy_wh,carbon,factor,(unsigned long long)b->valid_blocks,(unsigned long long)b->bad_blocks,(unsigned long long)b->gaps,
      b->phase,b->event_count,b->busy_drops,b->event.id,b->event.state,b->event.valid?"true":"false",
      ne_public_label(&b->event),b->event.reason,b->vrms,b->irms,b->baseline,b->threshold,
      b->confirm_blocks,CONFIRM_MAX_BLOCKS,b->confirm_retries,b->confirm_cancelled,b->confirm_timeouts);
    if(n<0||(size_t)n>=size)return n;
    for(unsigned k=0;k<b->trace_count;k++){
        const nb_trace *t=&b->trace[(b->trace_next+16-b->trace_count+k)%16];
        int q=snprintf(out+n,size-(size_t)n,"%s{\"ms\":%llu,\"seq\":%llu,\"phase\":%u,\"reason\":\"%s\",\"power_w\":%.9g,\"baseline_w\":%.9g,\"threshold_w\":%.9g}",
            k?",":"",(unsigned long long)t->ms,(unsigned long long)t->sequence,t->phase,t->reason,t->power,t->baseline,t->threshold);
        if(q<0)return q;
        n+=q;
        if((size_t)n>=size)return n;
    }
    int q=snprintf(out+n,size-(size_t)n,"]},\"event_diagnostics\":");
    if(q<0)return q;
    n+=q;
    if((size_t)n>=size)return n;
    q=ne_diagnostics_json(&b->event,out+n,size-(size_t)n);if(q<0)return q;n+=q;
    if((size_t)n>=size)return n;
    return n+snprintf(out+n,size-(size_t)n,"}");
}
