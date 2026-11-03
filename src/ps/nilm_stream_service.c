#include "nilm_stream_service.h"
#include "nilm_stream.h"
#include "nilm_model.h"
#include "nilm_infer.h"
#include "nilm_assets.h"
#include "nilm_net.h"
#include "nilm_status.h"
#include "nilm_event.h"
#include "nilm_board.h"
#include "nilm_dorm.h"
#include "nilm_store.h"
#ifdef NI_PL_BACKEND
#include "nilm_pl.h"
#endif
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "xparameters.h"
#include "xil_io.h"
#include "xil_mmu.h"
#include "xil_printf.h"
#define CSR_BASE 0x43c00000u
#define RAM_BASE 0x40000000u
#define HISTORY_BLOCKS 64u
/* Acquisition-only rolling history. No model/network consumer owns these yet.
 * Future consumers must add explicit pins or copy snapshots before wrap. */
static nilm_stream_sample history[HISTORY_BLOCKS][NILM_STREAM_SAMPLES];
static nilm_stream_desc descriptors[HISTORY_BLOCKS];
static nilm_stream stream;
/* Bring-up window only; not the final trained-model input contract. */
static nm_pipeline models;
static ni_engine inference;
static nb_board board;
#define event (board.event)
static uint64_t job_started,job_observed,completed_observed,last_latency,max_latency;
/* Cache completed result; engine workspace is reused by the next window. */
static char last_result[768]="null";
static uint64_t result_at,result_discontinuities;
static int model_selftest;
static unsigned learned_state,learned_label,last_match_event;

static uint64_t model_reported;
static uint32_t history_next,timer_hz;
static int active;
static uint32_t pl_magic,pl_abi,pl_samples;
static uint32_t read_csr(void *u,uint32_t a) { (void)u; return Xil_In32(CSR_BASE+a); }
static void write_csr(void *u,uint32_t a,uint32_t d) { (void)u; dsb(); Xil_Out32(CSR_BASE+a,d); dsb(); }
static uint32_t read_ram(void *u,uint32_t a) { (void)u; return Xil_In32(RAM_BASE+a); }
static uint64_t milliseconds64(void)
{
    uint32_t hi,lo,again;
    do { hi=Xil_In32(XPAR_GLOBAL_TIMER_BASEADDR+4); lo=Xil_In32(XPAR_GLOBAL_TIMER_BASEADDR); again=Xil_In32(XPAR_GLOBAL_TIMER_BASEADDR+4); } while(hi!=again);
    uint64_t ticks=((uint64_t)hi<<32)|lo;
    return (ticks/timer_hz)*1000u+((ticks%timer_hz)*1000u)/timer_hz;
}
static uint32_t milliseconds(void) {return (uint32_t)milliseconds64();}
int nilm_stream_service_active(void) { return active; }
void nilm_stream_service_init(void)
{
    uint32_t control=Xil_In32(XPAR_GLOBAL_TIMER_BASEADDR+8);
    timer_hz=XPAR_CPU_CORE_CLOCK_FREQ_HZ/(2u*(((control>>8)&255u)+1u));
    Xil_Out32(XPAR_GLOBAL_TIMER_BASEADDR+8,control|1u); dsb();
    Xil_SetTlbAttributes(CSR_BASE,DEVICE_MEMORY); Xil_SetTlbAttributes(RAM_BASE,DEVICE_MEMORY);
    pl_magic=read_csr(0,0);pl_abi=read_csr(0,4);pl_samples=read_csr(0,8);
    const nilm_stream_io io={0,read_csr,write_csr,read_ram};
    history_next=0;
    nb_init(&board);dorm_init();
    ns_init();while(ns_busy())ns_poll(); /* boot only, before acquisition */
    dorm_registry_sync(&board,milliseconds64());
    if(ns_active.factor_set)nb_set_factor(&board,ns_active.factor);
    xil_printf("NILM SD LEARNING: %s\r\n",ns_status());
    nm_init(&models,4096u);
    #ifdef NI_PL_BACKEND
    np_init(milliseconds64);
#endif
    model_selftest=ni_selftest(&inference,models.snapshot);
    board.enabled=model_selftest;
    xil_printf("NILM INFER SELFTEST: %s (CNN golden + KNN preprocess)\r\n",model_selftest?"PASS":"FAIL");
    const nm_backend backend={&inference,ni_submit,ni_poll,ni_cancel};
#ifdef NI_PL_BACKEND
    const nm_manifest manifest={NM_ABI,1u,4096u,NI_PREPROCESS_VERSION,NI_MODEL_VERSION,0u,1u};
#else
    const nm_manifest manifest={NM_ABI,1u,4096u,NI_PREPROCESS_VERSION,NI_MODEL_VERSION,0u,0u};
#endif
    if(!model_selftest || nm_bind_checked(&models,0,&backend,&manifest)!=0)
        xil_printf("NILM INFER: model compatibility failure\r\n");
    model_reported=0;
    xil_printf("NILM DORM: PS dynamic 14+N, PL CNN ABI2 logits+64D, no weight training; assets=" NI_ASSET_ID "; PCB_V2 gains; metrology UNVERIFIED\r\n");
    active=nilm_stream_init(&stream,&io,history[0],milliseconds())==0;
    if(active) xil_printf("NILM STREAM: RAW1 four banks, 1024 pairs/bank, DDR history=64 blocks; DORM status=9001 control=9002\r\n");
    else xil_printf("NILM STREAM INIT FAIL: error=%u faults=%x\r\n",(unsigned)stream.error,(unsigned)stream.hw_faults);
}
int nilm_device_json(char *out,size_t size)
{
#ifdef NI_PL_BACKEND
    int backend_ready=np_available();
    const char *backend="PL_CNN_PS_KNN",*pl_available=backend_ready?"true":"false";
#else
    int backend_ready=1;
    const char *backend="PS_REFERENCE",*pl_available="false";
#endif
    static char computation[16384],dorm[49152];nb_json(&board,computation,sizeof(computation));dorm_json(&board,dorm,sizeof(dorm),milliseconds64());
    return snprintf(out,size,
        "{\"schema\":1,\"board\":\"EES331\",\"firmware\":\"" NILM_FIRMWARE_VERSION "\",\"firmware_revision\":\"NLIM_set_fianl\","
        "\"wire_version\":1,\"boot_serial\":%u,\"uptime_ms\":%llu,\"pl_magic\":%u,\"pl_abi\":%u,\"pl_samples\":%u,"
        "\"ps_pl_compatible\":%s,\"stream_active\":%s,\"stream_error\":%u,\"hardware_faults\":%u,"
        "\"blocks\":%llu,\"gaps\":%llu,\"epoch\":%u,\"data_service\":%s,\"data_client\":%s,"
        "\"model_abi\":%u,\"window_samples\":%u,\"cnn_interface_abi\":2,\"preprocess_version\":2,\"input_units\":\"RAW1_to_PCB_V2_units\","
        "\"calibrated\":false,\"pl_inference_available\":%s,"
        "\"models\":[{\"name\":\"KNN\",\"loaded\":%s,\"compatibility\":%u,\"version\":%u,\"status\":%u},"
        "{\"name\":\"CNN\",\"loaded\":%s,\"compatibility\":%u,\"version\":%u,\"status\":%u}],"
        "\"inference\":{\"backend\":\"%s\",\"assets_id\":\"" NI_ASSET_ID "\",\"calibration_verified\":false,"
        "\"model_family\":\"FROZEN14_GATE_V3_PLUS_PAIRED_REGISTRY\",\"classes\":14,\"calibration_profile\":\"PCB_V2\","
        "\"unknown_supported\":true,\"unknown_assets_id\":\"3c3314d928437326\",\"registry_schema\":5,\"unknown_method\":\"EMBEDDING64_AUX_FOREST_V3\",\"unknown_scope\":\"EVENT_RESIDUAL_64D_AUX_BOARD_VARIANT\",\"unknown_validated\":false,\"fast_learning_supported\":true,\"automatic_dual_events_supported\":true,"
        "\"selftest_passed\":%s,\"stage\":%u,\"windows\":%llu,\"completed\":%llu,\"cnn_calls\":%llu,\"cnn_overrides\":%llu,\"cancelled\":%llu,"
        "\"skipped_blocks\":%llu,\"discontinuities\":%llu,\"last_latency_ms\":%llu,\"max_latency_ms\":%llu,"
        "\"result_current\":%s,\"result_age_ms\":%llu,\"last\":%s},"
        "\"board_compute\":%s,\"dorm\":%s,\"learning\":{\"location\":\"PS_SD\",\"store\":\"%s\",\"profiles\":%u,\"state\":%u,\"label\":\"%s\",\"validated\":false},\"automatic_event\":{\"supported\":true,\"port\":9002,\"id\":%u,\"busy\":%s,\"state_code\":%u,\"valid\":%s,\"label\":\"%s\",\"reason\":\"%s\"}}\n",
        (unsigned)ns_active.boot_serial,(unsigned long long)milliseconds64(),(unsigned)pl_magic,(unsigned)pl_abi,(unsigned)pl_samples,
        pl_magic==0x4e344231u && pl_abi==0x10000u && pl_samples==1024u?"true":"false",
        active?"true":"false",(unsigned)stream.error,(unsigned)stream.hw_faults,
        (unsigned long long)stream.blocks,(unsigned long long)stream.gap_blocks,(unsigned)stream.epoch,
        nilm_net_ready()?"true":"false",nilm_net_connected()?"true":"false",
        (unsigned)NM_ABI,(unsigned)models.target,pl_available,
        models.backend[0].submit?"true":"false",(unsigned)models.compatibility[0],
        (unsigned)models.manifest[0].model_version,(unsigned)models.result[0].status,
        models.backend[0].submit?"true":"false",(unsigned)models.compatibility[0],
        (unsigned)models.manifest[0].model_version,(unsigned)inference.cnn_status,
        backend,model_selftest?"true":"false",(unsigned)inference.stage,(unsigned long long)models.windows,(unsigned long long)inference.completed,
        (unsigned long long)inference.cnn_calls,(unsigned long long)inference.cnn_overrides,(unsigned long long)inference.cancelled,
        (unsigned long long)models.skipped_blocks,(unsigned long long)models.discontinuities,
        (unsigned long long)last_latency,(unsigned long long)max_latency,
        active && backend_ready && !ne_busy(&event) && completed_observed && result_discontinuities==models.discontinuities &&
        models.result[0].status!=NM_ERROR && milliseconds64()-result_at<2000u?"true":"false",
        (unsigned long long)(completed_observed?milliseconds64()-result_at:0),last_result,
        computation,dorm,ns_status(),(unsigned)ns_active.count,learned_state,learned_state==NL_CANDIDATE?ns_active.profiles[learned_label].name:"UNAVAILABLE",event.id,ne_busy(&event)?"true":"false",event.state,event.valid?"true":"false",
        ne_public_label(&event),event.reason);
}
int nilm_event_request(const char *command,char *reply,size_t size)
{
    if(!strcmp(command,"STATUS"))return ne_json(&event,reply,size);
    if(!strcmp(command,"BOARD")) {int n=nb_json(&board,reply,size);if(n>0 && (size_t)n+1<size){reply[n++]='\n';reply[n]=0;}return n;}
    if(!strcmp(command,"LEARNING")) {
        size_t used=0;int n=snprintf(reply,size,"{\"store\":\"%s\",\"profiles\":[",ns_status());if(n<0)return -1;used=(size_t)n;
        for(unsigned p=0;p<ns_active.count && used<size;p++) {
            n=snprintf(reply+used,size-used,"%s{\"name\":\"%s\",\"events\":%u,\"ready\":%s}",p?",":"",ns_active.profiles[p].name,(unsigned)ns_active.profiles[p].count,ns_active.profiles[p].count>0?"true":"false");
            if(n<0)return -1;
            used+=(size_t)n;
        }
        if(used>=size)return (int)used;
        n=snprintf(reply+used,size-used,"]}\n");return n<0?-1:(int)used+n;
    }
    double factor;char extra;
    if(sscanf(command,"FACTOR %lf %c",&factor,&extra)==1 && isfinite(factor)&&factor>=0&&factor<=5) {
        if(!ns_ready())return snprintf(reply,size,"{\"error\":\"STORE_NOT_READY\"}\n");
        ns_pending=ns_active;ns_pending.factor=factor;ns_pending.factor_set=1;
        if(ns_begin()!=0)return snprintf(reply,size,"{\"error\":\"SAVE_FAILED\"}\n");
        return snprintf(reply,size,"{\"accepted\":true,\"state\":\"SAVING\",\"effective\":\"AFTER_DURABLE_COMMIT\"}\n");
    }
    return dorm_command(&board,command,reply,size,milliseconds64());
}
void nilm_stream_service_poll(void)
{
#ifdef NI_PL_BACKEND
    np_tick();
#endif
    ns_poll();
    if(dorm_registry_sync(&board,milliseconds64())){nm_abort(&models);result_discontinuities=UINT64_MAX;learned_state=NL_NOT_READY;last_match_event=event.id;}
    if(ns_active.factor_set && (!board.factor_set || board.factor!=ns_active.factor))nb_set_factor(&board,ns_active.factor);
    if(!active) {board.power_valid=0;return;}
    nilm_stream_poll(&stream,milliseconds());
    if(stream.state==NS_BLOCK) {
        descriptors[history_next]=stream.desc;
        nilm_net_push(&stream.desc,history[history_next],milliseconds64(),stream.gap_blocks);
        int event_busy=ne_busy(&event);
        nb_push(&board,&stream.desc,history[history_next],milliseconds64());
        dorm_block(&board,milliseconds64());
        if(!event_busy && ne_busy(&event)){nm_abort(&models);result_discontinuities=UINT64_MAX;}
        event_busy=ne_busy(&event);
        if(event_busy)models.skipped_blocks++;
        else if(board.power_valid)nm_push(&models,&stream.desc,history[history_next]);
        else {nm_abort(&models);result_discontinuities=UINT64_MAX;}
        history_next=(history_next+1u)%HISTORY_BLOCKS;
        stream.buffer=history[history_next];
        if(stream.blocks==1 || stream.blocks%25u==0)
            xil_printf("NILM STREAM OK: blocks=%u seq=%u:%u gaps=%u epoch=%u last=%u\r\n",
              (unsigned)stream.blocks,(unsigned)(stream.desc.sequence>>32),(unsigned)stream.desc.sequence,
              (unsigned)stream.gap_blocks,(unsigned)stream.epoch,(unsigned)stream.desc.last_id);
    } else if(stream.state==NS_ERROR) {
        nilm_net_stop();
        nm_abort(&models);
        if(ne_busy(&event))ne_fail(&event,"STREAM_ERROR");
        active=0;
        xil_printf("NILM STREAM FAIL: error=%u faults=%x ack=%u copied=%u\r\n",
          (unsigned)stream.error,(unsigned)stream.hw_faults,(unsigned)stream.ack_result,(unsigned)stream.copied);
    } else if(stream.state==NS_STOPPED) { nilm_net_stop(); nm_abort(&models); if(ne_busy(&event))ne_fail(&event,"STREAM_STOPPED"); active=0; }
    if(models.windows!=job_observed){job_observed=models.windows;job_started=milliseconds64();}
    nb_poll(&board,milliseconds64());dorm_poll(&board,milliseconds64());
    if(event.state!=NE_DONE)learned_state=NL_NOT_READY;
    if(event.state==NE_DONE && event.id!=last_match_event) {
        last_match_event=event.id;learned_state=NL_NOT_READY;
        if(event.valid&&!strcmp(ne_identification(&event),"known")&&event.label>=14&&event.label<ND_CLASSES){learned_state=NL_CANDIDATE;learned_label=event.label-14;}
    }
    if(!ne_busy(&event))nm_poll(&models);
    if(inference.completed!=completed_observed){
        char ratio[40],confidence[32];
        completed_observed=inference.completed;result_at=milliseconds64();
        result_discontinuities=models.discontinuities;
        last_latency=result_at-job_started;if(last_latency>max_latency)max_latency=last_latency;
        if(inference.ratio>1e100)snprintf(ratio,sizeof(ratio),"null");
        else snprintf(ratio,sizeof(ratio),"%.12g",inference.ratio);
        if(inference.cnn_status==NM_DONE)snprintf(confidence,sizeof(confidence),"%.9g",(double)inference.cnn_confidence);
        else snprintf(confidence,sizeof(confidence),"null");
        int n=snprintf(last_result,sizeof(last_result),
            "{\"job\":%llu,\"epoch\":%u,\"first_id\":%u,\"last_id\":%u,\"sequence\":%llu,"
            "\"label\":\"%s\",\"route\":\"%s\",\"d1\":%.12g,\"ratio\":%s,\"rail_pairs\":%u,"
            "\"knn_label\":\"%s\",\"cnn_label\":\"%s\",\"cnn_confidence\":%s,\"polarity_flipped\":%s,"
            "\"training_vrms\":%.12g,\"training_irms\":%.12g,\"training_power\":%.12g}",
            (unsigned long long)inference.job,(unsigned)inference.last_epoch,(unsigned)inference.last_first_id,
            (unsigned)inference.last_last_id,(unsigned long long)inference.last_sequence,
            inference.label<NI_CLASSES?ni_classes[inference.label]:"INVALID",
            inference.route==NI_ROUTE_CNN_OVERRIDE?"CNN_OVERRIDE":(inference.route==NI_ROUTE_CNN_KEEP?"CNN_KEEP_KNN":"KNN"),inference.d1,ratio,
            (unsigned)inference.rail_samples,ni_classes[inference.knn_label],
            inference.cnn_status==NM_DONE && inference.cnn_label<NI_CLASSES?ni_classes[inference.cnn_label]:"NOT_CALLED",
            confidence,inference.polarity_flipped?"true":"false",inference.vrms,inference.irms,inference.power);
        if(n<0 || (unsigned)n>=sizeof(last_result))snprintf(last_result,sizeof(last_result),"null");
        if(completed_observed==1 || completed_observed%100u==0)
            xil_printf("NILM INFER OK: completed=%u route=%u label=%u latency_ms=%u skipped_blocks=%u\r\n",
                (unsigned)completed_observed,(unsigned)inference.route,(unsigned)inference.label,
                (unsigned)last_latency,(unsigned)models.skipped_blocks);
    }
    if(models.windows!=model_reported && (models.windows==1 || models.windows%100u==0)) {
        model_reported=models.windows;
        xil_printf("NILM MODEL: windows=%u cascade=%u CNN=%u discontinuities=%u skipped_blocks=%u\r\n",
            (unsigned)models.windows,(unsigned)models.result[0].status,(unsigned)inference.cnn_status,
            (unsigned)models.discontinuities,(unsigned)models.skipped_blocks);
    }
}


