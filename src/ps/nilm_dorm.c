/* Configurable policy/state/energy runs exclusively on PS, independent of browser.
 * Measurement aid, no automatic disconnection. Device attribution is estimated.
 * State is grouped by class: identical simultaneous appliances are not separable. */
#include "nilm_dorm.h"
#include "nilm_dynamic.h"
#include "nilm_dorm_assets.h"
#include "nilm_store.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define DEVICES (ND_CLASSES+1)
#define UNKNOWN_SLOT ND_CLASSES
#define LOGS 64
#define RECOVERY_BLOCKS 28u
#define QUIET_POWER_W 2.0
#define QUIET_CURRENT_A .05
typedef struct {unsigned on,restricted,anomaly;double power,display_power,wh,kg,ema,over_s;uint64_t changed;} device;
typedef struct {uint64_t time;unsigned id,event,ack;char kind[28],name[64];double power;} record;
typedef struct {uint64_t ms;unsigned event,matched;double measured,expected,limit;const char *reason;} recovery_trace;
static struct {
    device devices[DEVICES];record records[LOGS];
    unsigned next,count,serial,last_event,overload,unknown,quality,tracking_uncertain,revision,off_blocks,unresolved;
    uint64_t gaps,bad;unsigned busy_drops;double above_s,below_s,unassigned_wh,unassigned_w,unassigned_kg;
    unsigned recovery_event,recovery_blocks,recovery_class;
    uint64_t recovery_started,recovery_elapsed;
    double recovery_power,recovery_baseline,recovery_sum;
    unsigned recovery_samples;
    unsigned registry_initialized,registry_count,registry_revision;
    char registry_names[NL_PROFILES][64];
    const char *tracking_reason;
    const char *recovery_status,*recovery_reason;
    double recovery_measured;
    unsigned recovery_resets,recovery_max,recovery_next,recovery_count;
    recovery_trace recovery_trace[8];
} d;
static void log_event(const char *kind,const char *name,double power,unsigned event,uint64_t now){
    record *r=&d.records[d.next];memset(r,0,sizeof(*r));r->id=++d.serial;r->time=now;r->event=event;r->power=power;
    snprintf(r->kind,sizeof(r->kind),"%s",kind);snprintf(r->name,sizeof(r->name),"%s",name);
    d.next=(d.next+1)%LOGS;if(d.count<LOGS)d.count++;
}
static const char *name_of(unsigned c){return c==UNKNOWN_SLOT?"UNKNOWN":nd_name(c);}
static unsigned restricted(unsigned c){
    const char *s=name_of(c);for(unsigned i=0;i<ns_active.rule_count;i++)if(!strcmp(s,ns_active.restricted[i]))return 1;return 0;
}
static void recovery_note(const char *reason,const char *status,uint64_t now){
    recovery_trace *r=&d.recovery_trace[d.recovery_next];
    r->ms=now;r->event=d.recovery_event;r->matched=d.recovery_blocks;
    r->measured=d.recovery_measured;r->expected=d.recovery_power+d.recovery_baseline;
    r->limit=fmax(2,d.recovery_power*.05);r->reason=reason;
    d.recovery_next=(d.recovery_next+1)%8;if(d.recovery_count<8)d.recovery_count++;
    d.recovery_elapsed=now>=d.recovery_started?now-d.recovery_started:0;
    d.recovery_status=status;d.recovery_reason=reason;d.tracking_reason=reason;
}
static void recovery_cancel(const char *reason,uint64_t now){
    if(d.recovery_event)recovery_note(reason,"CANCELLED",now);
    d.recovery_event=d.recovery_blocks=d.recovery_samples=0;d.recovery_sum=0;
}
static void uncertain(const char *reason,uint64_t now){
    recovery_cancel(reason,now);
    d.tracking_uncertain=1;d.tracking_reason=reason;
    d.recovery_event=d.recovery_blocks=d.recovery_samples=0;d.recovery_sum=0;
    for(unsigned c=0;c<DEVICES;c++)d.devices[c].display_power=0;
}
void dorm_init(void){memset(&d,0,sizeof(d));uncertain("STARTUP_UNVERIFIED",0);}
int dorm_registry_sync(nb_board *b,uint64_t now){
    if(d.registry_initialized&&d.registry_revision==ns_active.revision)return 0;
    unsigned removed=d.registry_initialized&&ns_active.count<d.registry_count;
    if(removed){
        device kept[NL_PROFILES];memset(kept,0,sizeof(kept));
        for(unsigned c=0;c<ns_active.count;c++)for(unsigned k=0;k<d.registry_count;k++)
            if(!strcmp(ns_active.profiles[c].name,d.registry_names[k]))kept[c]=d.devices[14+k];
        memcpy(d.devices+14,kept,sizeof(kept));
        nb_registry_changed(b);d.last_event=b->event.id;uncertain("REGISTRY_CHANGED",now);
        log_event("REGISTRY_CHANGED","",0,b->event.id,now);
    }
    d.registry_initialized=1;d.registry_revision=ns_active.revision;d.registry_count=ns_active.count;
    for(unsigned c=0;c<ns_active.count;c++)strcpy(d.registry_names[c],ns_active.profiles[c].name);
    return removed;
}
static void invalidate(uint64_t now){
    if(!d.quality)log_event("DATA_INVALID","",0,0,now);
    recovery_cancel("RECOVERY_DATA_INVALID",now);d.quality=1;uncertain("DATA_INVALID",now);d.off_blocks=0;d.above_s=d.below_s=0;
    for(unsigned c=0;c<DEVICES;c++)d.devices[c].over_s=0;
}
/* This does not declare an arbitrary low-power load to be OFF. It permits a
 * fresh, known ON event from a quiet measured baseline to rebuild accounting.
 * All three physical before/after windows must support the same power change. */
static int recovery_evidence(const ne_event *e,double power,double *baseline){
    double sum=0;
    if(!e->direction||!isfinite(power)||power<5)return 0;
    for(unsigned k=0;k<3;k++){
        double ss=0;
        if(!e->prepared[k]||!e->prepared[k+3]||e->recognition[k]!=ND_KNOWN||e->labels[k]!=e->label)return 0;
        if(!isfinite(e->power[k])||e->power[k]<0||e->power[k]>QUIET_POWER_W||
           !isfinite(e->power[k+3])||!isfinite(e->vv[k])||e->vv[k]<NI_N)return 0;
        for(unsigned n=0;n<NI_N;n++)ss+=e->i[k][n]*e->i[k][n];
        if(!isfinite(ss)||sqrt(ss/NI_N)>QUIET_CURRENT_A)return 0;
        if(fabs(e->power[k+3]-e->power[k]-power)>fmax(2,power*.05))return 0;
        sum+=e->power[k];
    }
    *baseline=sum/3;return 1;
}
static void recover_block(const nb_board *b,uint64_t now){
    if(!d.recovery_event)return;
    const char *cancel=d.quality?"RECOVERY_DATA_INVALID":d.unknown?"RECOVERY_UNKNOWN_ACTIVE":
        b->event.id!=d.recovery_event?"RECOVERY_EVENT_CHANGED":
        ne_busy(&b->event)?"RECOVERY_EVENT_BUSY":b->phase==NB_CONFIRMING?"RECOVERY_CHANGE_PENDING":
        b->busy_drops!=d.busy_drops?"RECOVERY_OVERLAP":now-d.recovery_started>5000?"RECOVERY_TIMEOUT_5S":0;
    if(cancel){recovery_cancel(cancel,now);return;}
    unsigned active=0;
    for(unsigned c=0;c<ND_CLASSES;c++)active+=d.devices[c].on;
    if(active!=1||!d.devices[d.recovery_class].on){recovery_cancel("RECOVERY_ACTIVE_SET_CHANGED",now);return;}
    if(!isfinite(b->power)){recovery_cancel("RECOVERY_INVALID_POWER",now);return;}
    d.recovery_sum+=b->power;
    if(++d.recovery_samples<4)return;
    double measured=d.recovery_sum/4;d.recovery_samples=0;d.recovery_sum=0;d.recovery_measured=measured;
    double expected=d.recovery_power+d.recovery_baseline;
    if(fabs(measured-expected)>fmax(2,d.recovery_power*.05)){
        if(d.recovery_blocks||!d.recovery_reason||strcmp(d.recovery_reason,"RECOVERY_POWER_MISMATCH"))recovery_note("RECOVERY_POWER_MISMATCH","WAITING",now);
        if(d.recovery_blocks)d.recovery_resets++;
        d.recovery_blocks=0;return;
    }
    if(!d.recovery_blocks)recovery_note("RECOVERY_MATCHING","PENDING",now);
    d.recovery_blocks+=4;if(d.recovery_blocks>d.recovery_max)d.recovery_max=d.recovery_blocks;
    if(d.recovery_blocks<RECOVERY_BLOCKS)return;
    recovery_note("KNOWN_ON_POWER_BALANCE","RECOVERED",now);
    d.tracking_uncertain=0;d.tracking_reason="KNOWN_ON_POWER_BALANCE";
    log_event("TRACKING_RECOVERED",name_of(d.recovery_class),b->power,d.recovery_event,now);
    d.recovery_event=0;
    /* Previously unassigned energy is deliberately not reassigned retroactively. */
}
void dorm_block(const nb_board *b,uint64_t now){
    if(d.revision!=ns_active.revision){d.revision=ns_active.revision;d.above_s=d.below_s=0;}
    if(!b->power_valid||b->gaps!=d.gaps||b->bad_blocks!=d.bad){
        d.gaps=b->gaps;d.bad=b->bad_blocks;invalidate(now);
        if(b->power_valid){double wh=b->power*.04/3600;d.unassigned_wh+=wh;if(ns_active.factor_set)d.unassigned_kg+=wh*.001*ns_active.factor;}
        return;
    }
    if(d.quality){log_event("DATA_RESUMED","",b->power,0,now);d.quality=0;}
    double dt=.04;
    if(ns_active.limit_w>0){
        if(b->power>ns_active.limit_w){d.above_s+=dt;d.below_s=0;}
        else {d.above_s=0;if(b->power<ns_active.limit_w*ns_active.clear_ratio)d.below_s+=dt;else d.below_s=0;}
        if(!d.overload&&d.above_s+1e-9>=ns_active.confirm_s){d.overload=1;log_event("TOTAL_POWER_HIGH","",b->power,0,now);}
        if(d.overload&&d.below_s+1e-9>=ns_active.confirm_s){d.overload=0;log_event("TOTAL_POWER_CLEARED","",b->power,0,now);}
    }else {if(d.overload)log_event("POWER_RULE_DISABLED","",b->power,0,now);d.overload=0;d.above_s=d.below_s=0;}
    /* OFF gate from the supplied module: .020/.025 A hysteresis, one 4096-sample window. */
    if(b->irms<.020){if(d.off_blocks<4)d.off_blocks++;}else if(b->irms>.025)d.off_blocks=0;
    if(d.off_blocks>=4&&!ne_busy(&b->event)&&b->phase!=NB_CONFIRMING){
        for(unsigned c=0;c<DEVICES;c++)if(d.devices[c].on){
            d.devices[c].on=0;d.devices[c].power=0;d.devices[c].restricted=d.devices[c].anomaly=0;
            log_event("ALL_OFF",name_of(c),0,0,now);
        }
        d.unknown=0;d.tracking_uncertain=0;d.tracking_reason="CURRENT_OFF_GATE";
        recovery_cancel("RECOVERY_OFF_GATE",now);
    }
    recover_block(b,now);
    unsigned unresolved=d.tracking_uncertain&&b->power>5&&!ne_busy(&b->event)&&b->phase!=NB_CONFIRMING&&b->hcount>=25;
    if(unresolved&&!d.unresolved)log_event("UNIDENTIFIED_LOAD","",b->power,0,now);
    /* One aggregate attribution episode, not one new appliance per transient.
       Keep the latch across confirmation/checkpoint waits and power dips. */
    if(unresolved)d.unresolved=1;
    else if(!d.tracking_uncertain)d.unresolved=0;
    unsigned active=0,last=0,dynamic=0,dyn=0;double assigned=0;
    d.unknown=d.devices[UNKNOWN_SLOT].on;
    int trusted=!d.tracking_uncertain&&!d.unknown&&!ne_busy(&b->event)&&b->phase!=NB_CONFIRMING;
    for(unsigned c=0;c<ND_CLASSES;c++)if(d.devices[c].on){
        active++;if(d.devices[c].changed>=d.devices[last].changed)last=c;
        if(c<14&&nd_dynamic[c]){dynamic++;dyn=c;}
    }
    if(active==1&&trusted)d.devices[last].power=b->power;
    else if(active>1&&trusted){
        unsigned target=dynamic==1?dyn:last;double others=0;
        for(unsigned c=0;c<ND_CLASSES;c++)if(c!=target&&d.devices[c].on)others+=d.devices[c].power;
        if(others<=b->power)d.devices[target].power=b->power-others;
    }
    for(unsigned c=0;c<ND_CLASSES;c++)if(d.devices[c].on)assigned+=d.devices[c].power;
    /* Never allocate more than measured energy. During uncertainty retain totals only. */
    double scale=assigned>b->power&&assigned>0?b->power/assigned:1;
    if(d.tracking_uncertain||ne_busy(&b->event)||b->phase==NB_CONFIRMING)scale=0;
    double used=0;
    for(unsigned c=0;c<ND_CLASSES;c++){
        device *x=&d.devices[c];if(!x->on){x->anomaly=0;continue;}
        unsigned rule=restricted(c);if(rule&&!x->restricted)log_event("RESTRICTED_APPLIANCE",name_of(c),x->power,0,now);x->restricted=rule;
        
        double p=x->power*scale;x->display_power=p;used+=p;double wh=p*dt/3600;x->wh+=wh;
        if(ns_active.factor_set)x->kg+=wh*.001*ns_active.factor;
        if(!scale||!trusted){x->over_s=0;x->anomaly=0;continue;}
        x->ema+=(1-exp(-dt/.2))*(p-x->ema);
        if(c<14&&nd_upper[c]>0&&now-x->changed>=2500){
            if(x->ema>nd_upper[c])x->over_s+=dt;else x->over_s=0;
            if(x->over_s>=2&&!x->anomaly){x->anomaly=1;log_event("APPLIANCE_POWER_HIGH",name_of(c),x->ema,0,now);}
            if(x->anomaly&&x->ema<nd_upper[c]*.95){x->anomaly=0;log_event("APPLIANCE_POWER_CLEARED",name_of(c),x->ema,0,now);}
        }
    }
    d.unassigned_w=fmax(0,b->power-used);d.unassigned_wh+=d.unassigned_w*dt/3600;
    if(ns_active.factor_set)d.unassigned_kg+=d.unassigned_w*dt/3600*.001*ns_active.factor;
}
void dorm_poll(const nb_board *b,uint64_t now){
    if(!b->power_valid||now-b->last_input_ms>2000)invalidate(now);
    if(b->busy_drops!=d.busy_drops){d.busy_drops=b->busy_drops;uncertain("EVENT_BUSY_DROP",now);log_event("EVENT_BUSY_DROP","",b->power,0,now);}
    const ne_event *e=&b->event;
    if(!e->id||e->id==d.last_event||ne_busy(e))return;
    d.last_event=e->id;
    recovery_cancel("RECOVERY_EVENT_REPLACED",now);
    if(e->state!=NE_DONE||!e->valid){
        uncertain("EVENT_UNCERTAIN",now);log_event("EVENT_UNCERTAIN",e->last_failure&&strcmp(e->last_failure,"NONE")?e->last_failure:e->reason,0,e->id,now);return;
    }
    unsigned unknown=0,known=0;
    for(unsigned i=0;i<3;i++){unknown+=e->recognition[i]==ND_UNKNOWN;known+=e->recognition[i]==ND_KNOWN&&e->labels[i]==e->label;}
    if(unknown<2&&known<2){uncertain("AMBIGUOUS_EVENT",now);log_event("EVENT_UNCERTAIN","AMBIGUOUS",0,e->id,now);return;}
    if(unknown>=2){
        device *pool=&d.devices[UNKNOWN_SLOT];
        double power=(e->rp[0]+e->rp[1]+e->rp[2])/3;
        if(e->direction){pool->on=1;pool->power+=power;log_event("UNKNOWN_CHECK","",power,e->id,now);}
        else {pool->power=fmax(0,pool->power-power);if(pool->power<1)pool->on=0;log_event("UNIDENTIFIED_OFF","",power,e->id,now);}
        d.unknown=pool->on;return;
    }
    unsigned c=e->label;
    if(c>=DEVICES)return;
    double power=(e->rp[0]+e->rp[1]+e->rp[2])/3;
    device *x=&d.devices[c];
    if(e->direction){
        if(x->on){uncertain("REPEATED_CLASS",now);log_event("REPEATED_CLASS",name_of(c),power,e->id,now);return;}
        unsigned active=0;for(unsigned n=0;n<DEVICES;n++)active+=d.devices[n].on;
        double baseline=0;
        if(d.tracking_uncertain&&!d.quality&&b->power_valid&&now-b->last_input_ms<=2000&&
           !active&&c<ND_CLASSES&&recovery_evidence(e,power,&baseline)){
            d.recovery_event=e->id;d.recovery_class=c;d.recovery_started=now;
            d.recovery_power=power;d.recovery_baseline=baseline;d.recovery_measured=b->power;
            d.recovery_resets=d.recovery_max=0;recovery_note("RECOVERY_MATCHING","PENDING",now);
        }
        d.off_blocks=0;
        x->on=1;x->changed=e->mark_ms;x->power=power;x->ema=power;x->over_s=0;
        if(c==UNKNOWN_SLOT)log_event("UNKNOWN_CHECK",name_of(c),power,e->id,now);
        log_event("APPLIANCE_ON",name_of(c),power,e->id,now);
    }else {
        if(!x->on)log_event("OFF_WITHOUT_TRACKED_ON",name_of(c),power,e->id,now);
        x->on=0;x->power=0;x->restricted=x->anomaly=0;
        if(c==UNKNOWN_SLOT)log_event("UNKNOWN_OFF_CHECK",name_of(c),power,e->id,now);
        log_event("APPLIANCE_OFF",name_of(c),power,e->id,now);
    }
}
static void append(char *out,size_t cap,size_t *n,const char *fmt,...){
    if(*n>=cap)return;
    va_list a;va_start(a,fmt);int k=vsnprintf(out+*n,cap-*n,fmt,a);va_end(a);*n=k<0?cap:*n+(size_t)k;
}
int dorm_json(const nb_board *b,char *out,size_t cap,uint64_t now){
    size_t n=0;unsigned restricted_count=0,anomalies=0;for(unsigned c=0;c<DEVICES;c++){restricted_count+=d.devices[c].on&&d.devices[c].restricted;anomalies+=d.devices[c].on&&d.devices[c].anomaly;}
    append(out,cap,&n,"{\"schema\":1,\"compute\":\"BOARD_PS_PL\",\"hardware_validated\":false,\"unknown_validated\":false,\"event_pending\":%s,\"state_current\":%s,\"off\":%s,\"overload\":%s,\"unknown_active\":%s,\"restricted_count\":%u,\"anomaly_count\":%u,\"tracking_uncertain\":%s,\"unassigned_w\":%.9g,\"unassigned_wh\":%.9g,\"store\":\"%s\",\"config\":{\"revision\":%u,\"limit_w\":%.9g,\"confirm_s\":%.9g,\"clear_ratio\":%.9g,\"unknown_enabled\":%s,\"support_scale\":%.9g,\"restricted\":[",
        (ne_busy(&b->event)||b->phase==NB_CONFIRMING)?"true":"false",b->power_valid&&!d.quality&&now-b->last_input_ms<2000?"true":"false",d.off_blocks>=4?"true":"false",d.overload?"true":"false",d.unknown?"true":"false",restricted_count,anomalies,d.tracking_uncertain?"true":"false",d.unassigned_w,d.unassigned_wh,ns_status(),ns_active.revision,ns_active.limit_w,ns_active.confirm_s,ns_active.clear_ratio,ns_active.unknown_enabled?"true":"false",ns_active.support_scale);
    for(unsigned i=0;i<ns_active.rule_count;i++)append(out,cap,&n,"%s\"%s\"",i?",":"",ns_active.restricted[i]);
    append(out,cap,&n,"]},\"tracking\":{\"revision\":\"accounting-r3-windowed\",\"reason\":\"%s\",\"recovery_event\":%u,\"matched_blocks\":%u,\"required_blocks\":%u,\"quiet_power_limit_w\":%.3g,\"quiet_current_limit_a\":%.3g,\"retroactive_allocation\":false},\"devices\":[",
        d.tracking_reason?d.tracking_reason:"STARTUP_UNVERIFIED",d.recovery_event,d.recovery_blocks,RECOVERY_BLOCKS,QUIET_POWER_W,QUIET_CURRENT_A);unsigned count=0;
    for(unsigned c=0;c<ND_CLASSES;c++){const device *x=&d.devices[c];if(!x->on&&x->wh==0)continue;
        append(out,cap,&n,"%s{\"name\":\"%s\",\"on\":%s,\"power_w\":%.9g,\"energy_wh\":%.9g,\"carbon_kg\":%.9g,\"restricted\":%s,\"anomaly\":%s,\"estimated\":true}",count++?",":"",name_of(c),x->on?"true":"false",x->on?x->display_power:0,x->wh,x->kg,x->restricted?"true":"false",x->anomaly?"true":"false");
    }
    append(out,cap,&n,"],\"profiles\":[");
    for(unsigned c=0;c<ns_active.count;c++)append(out,cap,&n,"%s{\"name\":\"%s\",\"observations\":%u,\"ready\":%s,\"references\":%u,\"reference_limit\":12}",c?",":"",ns_active.profiles[c].name,ns_active.profiles[c].next,ns_active.profiles[c].count>0?"true":"false",ns_active.profiles[c].count);
    append(out,cap,&n,"],\"events\":[");
    unsigned visible=d.count;
    for(unsigned k=0;k<visible;k++){const record *r=&d.records[(d.next+LOGS-1-k)%LOGS];append(out,cap,&n,"%s{\"id\":%u,\"ms\":%llu,\"kind\":\"%s\",\"name\":\"%s\",\"power_w\":%.9g,\"event\":%u,\"ack\":%s}",k?",":"",r->id,(unsigned long long)r->time,r->kind,r->name,r->power,r->event,r->ack?"true":"false");}
    append(out,cap,&n,"],\"unidentified_load\":{\"active\":%s,\"power_w\":%.9g,\"energy_wh\":%.9g,\"carbon_kg\":%.9g},\"identification\":{\"status\":\"%s\",\"method\":\"EMBEDDING64_AUX_FOREST_V3\",\"unknown_validated\":false,\"event_id\":%u,\"windows\":[",d.unknown?"true":"false",d.unassigned_w,d.unassigned_wh,d.unassigned_kg,ne_identification(&b->event),b->event.id);
    for(unsigned k=0;k<3;k++){
        const ne_event *e=&b->event;char score[40]="null",candidate[40]="null",embedding[40]="null",auxiliary[40]="null",probability[40]="null";
        if(e->ready[k]&&isfinite(e->support_score[k])&&e->support_score[k]<1e100)snprintf(score,sizeof(score),"%.12g",e->support_score[k]);
        if(e->ready[k]&&isfinite(e->candidate_score[k])&&e->candidate_score[k]<1e100)snprintf(candidate,sizeof(candidate),"%.12g",e->candidate_score[k]);
        if(e->ready[k]&&isfinite(e->embedding_score[k])&&e->embedding_score[k]<1e100)snprintf(embedding,sizeof(embedding),"%.12g",e->embedding_score[k]);
        if(e->ready[k]&&isfinite(e->auxiliary_score[k]))snprintf(auxiliary,sizeof(auxiliary),"%.12g",e->auxiliary_score[k]);
        if(e->ready[k]&&isfinite(e->auxiliary_probability[k]))snprintf(probability,sizeof(probability),"%.12g",e->auxiliary_probability[k]);
        const ne_registry_diag *rd=&e->registry[k];
        char rb[40]="null",rr[40]="null",rs[40]="null",rm[40]="null";
        if(e->ready[k]&&isfinite(rd->paired_base)&&rd->paired_base<1e100)snprintf(rb,sizeof(rb),"%.12g",rd->paired_base);
        if(e->ready[k]&&isfinite(rd->paired_registered)&&rd->paired_registered<1e100)snprintf(rr,sizeof(rr),"%.12g",rd->paired_registered);
        if(e->ready[k]&&isfinite(rd->score)&&rd->score<1e100)snprintf(rs,sizeof(rs),"%.12g",rd->score);
        if(e->ready[k]&&isfinite(rd->margin)&&rd->margin<1e100)snprintf(rm,sizeof(rm),"%.12g",rd->margin);
        append(out,cap,&n,"%s{\"score\":%s,\"candidate_score\":%s,\"matched_class\":\"%s\",\"candidate_class\":\"%s\",\"reason\":\"%s\",\"known_support_calibrated\":%s,\"embedding_score\":%s,\"auxiliary_score\":%s,\"auxiliary_probability\":%s,\"registered_bypass\":%s,\"registry\":{\"references\":%u,\"revision\":%u,\"candidate\":\"%s\",\"base_paired_squared\":%s,\"registered_paired_squared\":%s,\"candidate_score\":%s,\"score_limit\":1.000000001,\"margin\":%s,\"margin_min\":1.2,\"reason\":\"%s\"}}",k?",":"",score,candidate,e->ready[k]&&e->support_match[k]<ND_CLASSES?nd_name(e->support_match[k]):"UNAVAILABLE",e->ready[k]?nd_name(e->labels[k]):"UNAVAILABLE",e->ready[k]&&e->reject_reason[k]?e->reject_reason[k]:"PENDING",e->ready[k]&&e->support_calibrated[k]?"true":"false",embedding,auxiliary,probability,e->ready[k]&&e->auxiliary_bypassed[k]?"true":"false",e->ready[k]?rd->count:0,e->ready[k]?rd->revision:0,e->ready[k]&&rd->candidate>=14&&rd->candidate<14+ns_active.count?nd_name(rd->candidate):"UNAVAILABLE",rb,rr,rs,rm,e->ready[k]&&rd->reason?rd->reason:"PENDING");
    }
    append(out,cap,&n,"]}");
    append(out,cap,&n,",\"recovery_diagnostics\":{\"status\":\"%s\",\"reason\":\"%s\",\"expected_w\":%.9g,\"measured_w\":%.9g,\"tolerance_w\":%.9g,\"timeout_ms\":5000,\"power_window_blocks\":4,\"elapsed_ms\":%llu,\"max_matched_blocks\":%u,\"reset_count\":%u,\"trace\":[",d.recovery_status?d.recovery_status:"IDLE",d.recovery_reason?d.recovery_reason:"NONE",d.recovery_power+d.recovery_baseline,isfinite(d.recovery_measured)?d.recovery_measured:0,fmax(2,d.recovery_power*.05),(unsigned long long)(d.recovery_event?now-d.recovery_started:d.recovery_elapsed),d.recovery_max,d.recovery_resets);
    for(unsigned k=0;k<d.recovery_count;k++){
        const recovery_trace *r=&d.recovery_trace[(d.recovery_next+8-d.recovery_count+k)%8];
        append(out,cap,&n,"%s{\"ms\":%llu,\"event\":%u,\"reason\":\"%s\",\"matched_before\":%u,\"measured_w\":%.9g,\"expected_w\":%.9g,\"tolerance_w\":%.9g}",k?",":"",(unsigned long long)r->ms,r->event,r->reason,r->matched,isfinite(r->measured)?r->measured:0,r->expected,r->limit);
    }
    append(out,cap,&n,"]}");
    append(out,cap,&n,",\"enroll_event\":%u,\"enroll_available\":%s,\"enroll_direction\":\"%s\",\"advice\":\"%s\",\"accounting_scope\":\"BOOT_VALID_DATA_ESTIMATED_CLASS_ALLOCATION\"}",b->event.id,b->power_valid&&!d.quality&&now-b->event.mark_ms<120000&&b->event.state==NE_DONE&&b->event.valid?"true":"false",b->event.direction?"ON":"OFF",d.overload?"REDUCE_TOTAL_LOAD":restricted_count?"CHECK_RESTRICTED_APPLIANCE":d.unknown?"INSPECT_UNKNOWN_LOAD":anomalies?"CHECK_DEVICE_POWER":d.tracking_uncertain?"WAIT_FOR_CLEAN_SWITCH_EVENT":"TURN_OFF_UNUSED_APPLIANCES");
    return (int)n;
}
static int decode(const char *hex,char *name){
    size_t len=strlen(hex);if(!len||len>126||len%2)return 0;
    for(size_t n=0;n<len;n+=2){unsigned v=0;for(unsigned k=0;k<2;k++){char c=hex[n+k];unsigned q=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:99;if(q>15)return 0;v=v*16+q;}if(!v)return 0;name[n/2]=(char)v;}name[len/2]=0;return nl_name_ok(name);
}
int dorm_command(const nb_board *b,const char *cmd,char *out,size_t cap,uint64_t now){
    char extra,hex[127],name[64];unsigned add,id,on;double limit,confirm,ratio,scale;
    if(!strcmp(cmd,"DORM")){int n=dorm_json(b,out,cap,now);if(n>0&&(size_t)n+1<cap){out[n++]='\n';out[n]=0;}return n;}
    if(sscanf(cmd,"ACK %u %c",&id,&extra)==1){for(unsigned k=0;k<d.count;k++){record *r=&d.records[k];if(r->id==id){r->ack=1;return snprintf(out,cap,"{\"accepted\":true}\n");}}return snprintf(out,cap,"{\"error\":\"RECORD_EXPIRED\"}\n");}
    if(!ns_ready())return snprintf(out,cap,"{\"error\":\"STORE_NOT_READY\"}\n");
    ns_pending=ns_active;
    if(sscanf(cmd,"CONFIG %lf %lf %lf %u %lf %c",&limit,&confirm,&ratio,&on,&scale,&extra)==5){
        ns_pending.limit_w=limit;ns_pending.confirm_s=confirm;ns_pending.clear_ratio=ratio;ns_pending.unknown_enabled=on;ns_pending.support_scale=scale;ns_pending.revision++;
        if(!nl_validate(&ns_pending))return snprintf(out,cap,"{\"error\":\"INVALID_CONFIG\"}\n");
    }else if(sscanf(cmd,"RULE %u %126s %c",&add,hex,&extra)==2){
        if(!decode(hex,name)||nl_rule(&ns_pending,name,add)<0)return snprintf(out,cap,"{\"error\":\"INVALID_RULE\"}\n");
        ns_pending.revision=ns_active.revision+1;
    }else if(sscanf(cmd,"REGDELETE %u %126s %c",&id,hex,&extra)==2){
        if(id!=ns_active.revision)return snprintf(out,cap,"{\"error\":\"STALE_REVISION\"}\n");
        if(!decode(hex,name))return snprintf(out,cap,"{\"error\":\"INVALID_NAME\"}\n");
        int code=nl_delete(&ns_pending,name);
        if(code)return snprintf(out,cap,"{\"error\":\"REGISTRY_DELETE_FAILED\",\"code\":%d}\n",code);
    }else if(sscanf(cmd,"REGCLEAR %u %126s %c",&id,hex,&extra)==2){
        if(strcmp(hex,"CONFIRM"))return snprintf(out,cap,"{\"error\":\"CONFIRM_REQUIRED\"}\n");
        if(id!=ns_active.revision)return snprintf(out,cap,"{\"error\":\"STALE_REVISION\"}\n");
        if(!ns_active.count)return snprintf(out,cap,"{\"accepted\":true,\"state\":\"UNCHANGED\"}\n");
        if(nl_clear(&ns_pending))return snprintf(out,cap,"{\"error\":\"REGISTRY_DELETE_FAILED\"}\n");
    }else if(sscanf(cmd,"ENROLL %u %126s %c",&id,hex,&extra)==2){
        const ne_event *e=&b->event;
        if(!b->power_valid||d.quality||now-e->mark_ms>=120000||id!=e->id||e->state!=NE_DONE||!e->valid)return snprintf(out,cap,"{\"error\":\"NEED_CURRENT_VALID_EVENT\"}\n");
        if(!decode(hex,name))return snprintf(out,cap,"{\"error\":\"INVALID_NAME\"}\n");
        int count=nl_enroll(&ns_pending,name,((uint64_t)ns_active.boot_serial<<32)|id,e->features[1],e->embeddings[1]);
        if(count<0)return snprintf(out,cap,"{\"error\":\"ENROLL_REJECTED\",\"code\":%d}\n",count);
    }else return snprintf(out,cap,"{\"error\":\"INVALID_COMMAND\"}\n");
    if(ns_begin())return snprintf(out,cap,"{\"error\":\"SAVE_FAILED\"}\n");
    return snprintf(out,cap,"{\"accepted\":true,\"state\":\"SAVING\",\"effective\":\"AFTER_DURABLE_COMMIT\"}\n");
}
