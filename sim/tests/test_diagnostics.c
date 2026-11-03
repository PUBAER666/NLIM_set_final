#include "nilm_board.h"
#include "nilm_dorm.h"
#include "nilm_dynamic.h"
#include "nilm_store.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static nb_board b;
static char json[49152];
static uint64_t now;
static void snapshot(void){int n=dorm_json(&b,json,sizeof(json),now);assert(n>0&&n<(int)sizeof(json));}
static int has(const char *s){snapshot();return strstr(json,s)!=0;}
static void block(double power){now+=40;b.power=power;b.irms=.16;b.vrms=233;b.power_valid=1;b.last_input_ms=now;dorm_block(&b,now);dorm_poll(&b,now);}
static void candidate(void){
    ns_init();nb_init(&b);dorm_init();now=100;b.power_valid=1;b.last_input_ms=now;b.power=.7;
    ne_event *e=&b.event;e->state=NE_DONE;e->id=1;e->valid=1;e->direction=1;e->label=7;e->mark_ms=now;
    for(unsigned k=0;k<3;k++){
        e->recognition[k]=ND_KNOWN;e->labels[k]=7;e->rp[k]=18.3428294;e->prepared[k]=e->prepared[k+3]=1;
        e->power[k]=.7;e->power[k+3]=19.0428294;e->vv[k]=233.*233*NI_N;
        for(unsigned n=0;n<NI_N;n++)e->i[k][n]=(n&1)?.034:-.034;
    }
    dorm_poll(&b,now);assert(has("RECOVERY_MATCHING"));
}
static void source(unsigned unstable_side){
    nb_init(&b);ne_event *e=&b.event;e->id=1;e->state=NE_PREP;e->start_ms=e->last_rx_ms=100;
    for(unsigned w=0;w<6;w++){
        double amp=100;
        if((unstable_side&1)&&w==0)amp=10;
        if((unstable_side&2)&&w==3)amp=10;
        for(unsigned n=0;n<NI_N;n++){
            double x=sin(6.283185307179586*n/512);
            unsigned v=(unsigned)lround(2048+1000*x),i=(unsigned)lround(2048+amp*x);
            e->raw[w][n]=v<<16|i;
        }
        e->first_id[w]=w*NI_N;
    }
    for(unsigned i=0;i<2000&&e->state==NE_PREP;i++)ne_poll(e,100);
    assert(e->source.checked&&e->source.failed_mask==unstable_side);
    assert(e->state==(unstable_side?NE_ERROR:NE_PHASE));
}
int main(void){
    for(unsigned side=0;side<4;side++){
        source(side);ne_record_checkpoint(&b.event,1,b.event.reason);
        int n=ne_diagnostics_json(&b.event,json,sizeof(json));assert(n>0&&n<(int)sizeof(json));
        assert(strstr(json,"stddev_w")&&strstr(json,"first_ids"));puts(json);
    }
    source(1);b.checkpoint=6;nb_poll(&b,100);
    assert(!strcmp(b.event.reason,"BACKGROUND_UNSTABLE"));
    assert(!strcmp(b.event.last_failure,"BACKGROUND_UNSTABLE"));
    assert(b.event.checkpoint_count==1&&b.event.checkpoints[0].failed_mask==1);
    /* New event AND new checkpoint must not expose previous model metrics. */
    b.event.auxiliary_probability[0]=.98;b.event.auxiliary_bypassed[0]=1;b.event.ready[0]=1;
    char reply[8192];ne_command(&b.event,"ARM 2",reply,sizeof(reply),100,1);
    assert(!b.event.ready[0]&&!b.event.auxiliary_probability[0]&&!b.event.auxiliary_bypassed[0]);
    assert(has("\"auxiliary_probability\":null,\"registered_bypass\":false"));
    b.event.auxiliary_probability[0]=.88;b.event.ready[0]=1;ne_reset_results(&b.event);
    assert(!b.event.auxiliary_probability[0]&&!b.event.ready[0]);
    /* Reproduce a 12-block run, a full mismatching window, then the bounded 5s timeout. */
    candidate();for(unsigned k=0;k<12;k++)block(19.04);
    assert(has("\"matched_blocks\":12"));for(unsigned k=0;k<4;k++)block(16);
    assert(has("\"matched_blocks\":0")&&has("RECOVERY_POWER_MISMATCH")&&has("\"matched_before\":12"));
    assert(has("\"max_matched_blocks\":12")&&has("\"reset_count\":1"));
    for(unsigned k=0;k<130;k++)block(16);
    assert(has("\"status\":\"CANCELLED\"")&&has("RECOVERY_TIMEOUT_5S")&&has("\"tracking_uncertain\":true"));puts(json);
    candidate();for(unsigned k=0;k<28;k++)block(19.04);
    assert(has("\"status\":\"RECOVERED\"")&&has("\"tracking_uncertain\":false"));
    /* Alternating block ripple averages correctly without relaxing absolute tolerance. */
    candidate();for(unsigned k=0;k<28;k++)block(k%2?22.04:16.04);
    assert(has("\"status\":\"RECOVERED\"")&&has("\"reset_count\":0"));
    candidate();block(19.04);b.phase=NB_CONFIRMING;block(19.04);assert(has("RECOVERY_CHANGE_PENDING"));
    candidate();b.busy_drops++;block(19.04);assert(has("RECOVERY_OVERLAP"));
    candidate();b.gaps++;block(19.04);assert(has("RECOVERY_DATA_INVALID"));
    candidate();b.event.id=2;block(19.04);assert(has("RECOVERY_EVENT_CHANGED"));
    /* Repeated transient/confirmation states remain one attribution episode. */
    nb_init(&b);dorm_init();now=100;b.hcount=25;
    for(unsigned k=0;k<10;k++){
        b.phase=NB_ARMED;block(20);b.phase=NB_CONFIRMING;block(20);block(1);
    }
    snapshot();unsigned count=0;const char *p=json;
    while((p=strstr(p,"\"kind\":\"UNIDENTIFIED_LOAD\""))){count++;p++;}assert(count==1);
    puts("PASS source-side metrics, thresholds, checkpoint root cause, stale-field reset, windowed recovery reset/timeout and ripple averaging, recovery cancellation reasons, successful recovery and unassigned-episode deduplication");
}

