#define __USE_MINGW_ANSI_STDIO 1
#include <assert.h>
#include <string.h>
#include "nilm_stream_service.c"
nl_bank ns_active,ns_pending;
void ns_init(void){nl_init(&ns_active);ns_active.boot_serial=1;}
int ns_begin(void){ns_active=ns_pending;return 0;}
void ns_poll(void){}
int ns_busy(void){return 0;}
int ns_ready(void){return 1;}
const char *ns_status(void){return "READY";}
uint32_t Xil_In32(uint32_t address){return address==XPAR_GLOBAL_TIMER_BASEADDR?10000:0;}
void Xil_Out32(uint32_t a,uint32_t b){(void)a;(void)b;}
int nilm_net_ready(void){return 1;}
int nilm_net_connected(void){return 0;}
void nilm_net_stop(void){}
void nilm_net_push(const nilm_stream_desc *a,const nilm_stream_sample *b,uint64_t c,uint64_t d){(void)a;(void)b;(void)c;(void)d;}
int nilm_stream_init(nilm_stream *s,const nilm_stream_io *io,nilm_stream_sample *b,uint32_t t){(void)s;(void)io;(void)b;(void)t;return 0;}
void nilm_stream_poll(nilm_stream *s,uint32_t t){(void)s;(void)t;}
int main(void) {
    char json[65536],tiny[8];nilm_stream_service_init();assert(model_selftest);
    board.energy_wh=12.5;board.power=123;board.power_valid=1;
    int n=nilm_device_json(json,sizeof(json));assert(n>0&&n<65536&&json[n-1]=='\n');puts(json);
    assert(nilm_device_json(tiny,sizeof(tiny))>=8);
    nilm_event_request("FACTOR nan",json,sizeof(json));assert(strstr(json,"error"));
    nilm_event_request("FACTOR 0.5 extra",json,sizeof(json));assert(strstr(json,"error"));
    nilm_event_request("FACTOR 0.5",json,sizeof(json));assert(strstr(json,"accepted")&&ns_active.factor==.5);
    nilm_event_request("ENROLL 1 616263",json,sizeof(json));assert(strstr(json,"NEED_CURRENT_VALID_EVENT"));
    event.id=1;event.state=NE_DONE;event.valid=1;
    nilm_event_request("ENROLL 1 e794b5e6b0b4e5a3b6",json,sizeof(json));assert(strstr(json,"accepted") && ns_active.count==1);
    nilm_event_request("ENROLL 1 616263",json,sizeof(json));assert(strstr(json,"ENROLL_REJECTED"));
    nilm_event_request("LEARNING",json,sizeof(json));puts(json);
    nilm_event_request("ENROLL 1 ff",json,sizeof(json));assert(strstr(json,"INVALID_NAME"));
    /* ON registration, revision-guarded deletion, cached event invalidation. */
    event.id=2;event.state=NE_DONE;event.valid=1;event.direction=1;
    nilm_event_request("ENROLL 2 7365636f6e64",json,sizeof(json));assert(strstr(json,"accepted")&&ns_active.count==2);
    dorm_registry_sync(&board,0);
    unsigned rev=ns_active.revision;char cmd[160];
    snprintf(cmd,sizeof(cmd),"REGDELETE %u 7365636f6e64",rev+1);
    nilm_event_request(cmd,json,sizeof(json));assert(strstr(json,"STALE_REVISION")&&ns_active.count==2);
    snprintf(cmd,sizeof(cmd),"REGDELETE %u 7365636f6e64",rev);
    nilm_event_request(cmd,json,sizeof(json));assert(strstr(json,"accepted")&&ns_active.count==1);
    assert(dorm_registry_sync(&board,0)==1&&event.state==NE_ERROR&&!event.valid);
    nilm_event_request("ENROLL 2 7365636f6e64",json,sizeof(json));assert(strstr(json,"NEED_CURRENT_VALID_EVENT"));
    snprintf(cmd,sizeof(cmd),"REGCLEAR %u NO",ns_active.revision);
    nilm_event_request(cmd,json,sizeof(json));assert(strstr(json,"CONFIRM_REQUIRED")&&ns_active.count==1);
    snprintf(cmd,sizeof(cmd),"REGCLEAR %u CONFIRM",ns_active.revision);
    nilm_event_request(cmd,json,sizeof(json));assert(strstr(json,"accepted")&&!ns_active.count);
    assert(dorm_registry_sync(&board,0)==1);
    snprintf(cmd,sizeof(cmd),"REGCLEAR %u CONFIRM",ns_active.revision);
    nilm_event_request(cmd,json,sizeof(json));assert(strstr(json,"UNCHANGED"));
    nl_init(&ns_active);ns_active.count=32;
    for(unsigned p=0;p<32;p++){
        nl_profile *x=&ns_active.profiles[p];snprintf(x->name,64,"Profile%02u_abcdefghijklmnopqrstuvwxyz_ABCDEFGHIJKLMNOPQRSTUVWX",p);
        x->count=x->next=8;x->last_key=100+p;x->selected[0]=0;x->selected[1]=1;x->selected[2]=2;x->radius5=x->radius64=1;
        for(unsigned k=0;k<8;k++)x->examples[k].key=1+p*8+k;
    }
    assert(nl_validate(&ns_active));
    for(unsigned p=0;p<32;p++)assert(nl_rule(&ns_active,ns_active.profiles[p].name,1)==0);
    for(unsigned p=0;p<46;p++){
        event.id=100+p;event.state=NE_DONE;event.valid=1;event.label=p;event.direction=1;
        for(unsigned k=0;k<3;k++){event.labels[k]=p;event.recognition[k]=ND_KNOWN;event.rp[k]=10;}
        dorm_poll(&board,0);
    }
    board.irms=1;dorm_block(&board,0);
    board.trace_count=16;board.trace_next=0;
    for(unsigned k=0;k<16;k++){
        board.trace[k].ms=18446744073709551615ull;board.trace[k].sequence=18446744073709551615ull;
        board.trace[k].reason="CHANGE_RETURNED_TO_BASELINE";board.trace[k].power=20000;
    }
    for(unsigned c=1;c<=6;c++){
        event.source.checked=1;event.source.failed_mask=3;
        for(unsigned k=0;k<6;k++){event.first_id[k]=4294967295u;event.power[k]=12345.6789;}
        for(unsigned k=0;k<2;k++){event.source.mean[k]=10000;event.source.stddev[k]=1000;event.source.limit[k]=100;}
        ne_record_checkpoint(&event,c,"CHECKPOINT_DECISIONS_DISAGREE");
    }
    n=nilm_device_json(json,sizeof(json));assert(n>0&&n<65536);printf("MAX_STATUS_BYTES=%d\n",n);puts(json);
    puts("STATUS_COMMAND_TEST_PASS");
}
