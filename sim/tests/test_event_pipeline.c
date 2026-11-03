#include "nilm_board.h"
#include "nilm_store.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static nb_board b;static nilm_stream_sample s[1024];
static unsigned poll_enabled=1;
static void feed(unsigned seq,double amp,unsigned rail){
    for(unsigned n=0;n<1024;n++){double x=sin(6.283185307179586*n/512);unsigned v=(unsigned)lround(2048+1000*x),i=(unsigned)lround(2048+amp*x);s[n].data=v<<16|i;s[n].id=seq*1024+n;}
    if(rail)s[50].data=0;
    nilm_stream_desc d={1,0,seq*1024,seq*1024+1023,seq};unsigned now=seq*40;
    nb_push(&b,&d,s,now);unsigned guard=0;
    while(poll_enabled&&b.event.state>=NE_PREP&&ne_busy(&b.event)&&guard++<300000)nb_poll(&b,now);
    assert(guard<300000);
}
int main(void){
    ns_init();nb_init(&b);for(unsigned n=0;n<40;n++)feed(n,100,0);
    assert(b.phase==NB_ARMED);
    for(unsigned n=40;n<60;n++)feed(n,50,0);
    assert(b.event_count==1&&b.event.direction==0&&ne_busy(&b.event));
    assert(b.checkpoint==2&&!b.event.valid); /* first provisional result is never final */
    for(unsigned n=60;n<84;n++)feed(n,50,0);
    assert(b.event.state==NE_DONE&&b.event.valid&&b.checkpoint==0);
    assert(b.event.phase_job.state==5&&b.event.rp[0]>80);
    double wh=b.energy_wh;feed(84,50,1);assert(!b.power_valid&&b.energy_wh==wh&&b.bad_blocks==1);
    feed(86,50,0);assert(b.gaps==1);nb_poll(&b,10000);assert(!b.power_valid);
    nb_init(&b);for(unsigned n=0;n<40;n++)feed(n,50,0);for(unsigned n=40;n<84;n++)feed(n,100,0);
    assert(b.event.state==NE_DONE&&b.event.valid&&b.event.direction==1&&b.event.rp[0]>80);
    nb_init(&b);for(unsigned n=0;n<40;n++)feed(n,100,0);for(unsigned n=40;n<48;n++)feed(n,50,0);feed(49,50,0);
    assert(b.event.state==NE_ERROR&&!b.collecting);
    /* Staged startup used to reset to warmup at block 47 and silently learn
       the ON plateau as the baseline. The original trigger must be retained. */
    poll_enabled=0;nb_init(&b);
    for(unsigned n=0;n<40;n++)feed(n,0,0);
    for(unsigned n=40;n<45;n++)feed(n,8,0);
    for(unsigned n=45;n<50;n++)feed(n,16,0);
    assert(b.event_count==1&&b.trigger==40&&b.confirm==49&&b.event.direction==1);
    assert(b.confirm_retries==2&&b.event.first_id[0]==26*1024);
    assert(b.event.raw_ready[0]&&b.event.raw_ready[1]&&b.event.raw_ready[2]);
    /* A short transient returning to baseline must not introduce a one-second blind interval. */
    nb_init(&b);for(unsigned n=0;n<40;n++)feed(n,0,0);
    feed(40,8,0);feed(41,0,0);assert(b.phase==NB_ARMED&&b.confirm_cancelled==1);
    for(unsigned n=42;n<50;n++)feed(n,16,0);
    assert(b.event_count==1&&b.trigger==42);
    assert(b.event.first_id[0]==26*1024&&b.post_first==50);
    for(unsigned w=0;w<3;w++)for(unsigned n=0;n<NI_N;n++)assert((b.event.raw[w][n]&4095u)==2048);
    /* Non-settling startup is bounded and explicitly diagnosed, never consumes stale banks. */
    nb_init(&b);for(unsigned n=0;n<40;n++)feed(n,0,0);
    for(unsigned n=40;n<72;n++)feed(n,n%2?8:20,0);
    assert(b.event_count==0&&b.confirm_timeouts==1&&b.phase==NB_WARMUP);
    char json[8192];int size=nb_json(&b,json,sizeof(json));assert(size>0&&size<(int)sizeof(json));
    assert(strstr(json,"CONFIRM_TIMEOUT")&&strstr(json,"ON_TRIGGER")&&strstr(json,"current_rms_a"));
    puts("PASS phase-median ON/OFF, checkpoints, ADC/gap/stale guards; staged startup retains trigger+prehistory, transient rearm, bounded confirmation timeout and diagnostics");
}
