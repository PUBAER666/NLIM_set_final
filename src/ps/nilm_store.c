#include "nilm_store.h"
#include "ff.h"
#include "nilm_legacy.h"
#include <string.h>
#include <stddef.h>
nl_bank ns_active,ns_pending;
static FATFS fs;
static FIL file;
static const char *paths[]={"0:/NILM430.BIN","0:/NILM431.BIN","0:/NILMV40.BIN","0:/NILMV41.BIN","0:/DORM0.BIN","0:/DORM1.BIN"};
static old_nl_bank legacy_bank;
typedef struct {uint32_t magic,schema,size,crc;uint64_t generation;} header;
static header head;
static uint64_t generation;
static unsigned selected,target,offset,state,available;
static uint32_t check_crc;
static const char *status="NOT_INITIALIZED";
static uint32_t crc_update(uint32_t crc,const void *data,unsigned size) {
    const unsigned char *p=data;
    for(unsigned i=0;i<size;i++){crc^=p[i];for(unsigned b=0;b<8;b++)crc=(crc>>1)^(0xedb88320u & (0u-(crc&1)));}return crc;
}
static uint32_t checksum(const header *h,const nl_bank *bank) {
    uint32_t c=crc_update(~0u,&h->generation,sizeof(h->generation));
    return ~crc_update(c,bank,sizeof(*bank));
}
static int load(unsigned slot,nl_bank *b,header *h) {
    FIL f;UINT n;
    FRESULT r=f_open(&f,paths[slot],FA_READ);
    if(r==FR_NO_FILE)return 0;
    if(r!=FR_OK)return -1;
    if(slot>=2){
        int ok=f_size(&f)==sizeof(*h)+sizeof(legacy_bank)&&f_read(&f,h,sizeof(*h),&n)==FR_OK&&n==sizeof(*h);
        if(ok)ok=h->magic==0x314c494eu&&h->schema==1&&h->size==sizeof(legacy_bank)&&
            f_read(&f,&legacy_bank,sizeof(legacy_bank),&n)==FR_OK&&n==sizeof(legacy_bank);
        if(ok){
            uint32_t c=crc_update(~0u,&h->generation,sizeof(h->generation));
            ok=(~crc_update(c,&legacy_bank,sizeof(legacy_bank))==h->crc)&&nl_import_v4(b,&legacy_bank,sizeof(legacy_bank));
        }
        if(f_close(&f)!=FR_OK)ok=0;
        return ok?1:-1;
    }
    if(f_size(&f)<sizeof(*h)+sizeof(*b)){f_close(&f);return -2;}
    int ok=f_size(&f)==sizeof(*h)+sizeof(*b) && f_read(&f,h,sizeof(*h),&n)==FR_OK && n==sizeof(*h);
    if(ok)ok=h->magic==0x314c494eu && h->schema==1 && h->size==sizeof(*b) &&
        f_read(&f,b,sizeof(*b),&n)==FR_OK && n==sizeof(*b) && checksum(h,b)==h->crc && (slot<2?nl_validate(b):nl_validate_legacy(b));
    if(f_close(&f)!=FR_OK)ok=0;
    return ok?1:-1;
}
int ns_busy(void){return state!=0;}
int ns_ready(void){return available && !state;}
const char *ns_status(void){return status;}
int ns_begin(void) {
    if(!ns_ready()||!nl_validate(&ns_pending)||generation==UINT64_MAX)return -1;
    head=(header){0x314c494e,1,sizeof(nl_bank),0,generation+1};head.crc=checksum(&head,&ns_pending);
    target=selected^1;offset=0;state=1;status="SAVING";return 0;
}
void ns_init(void) {
    nl_init(&ns_active);nl_init(&ns_pending);state=available=0;selected=0;generation=0;
    if(f_mount(&fs,"0:/",1)!=FR_OK){status="SD_UNAVAILABLE";return;}
    header a,b;int ra=load(0,&ns_active,&a),rb=load(1,&ns_pending,&b);
    if(ra==1 && (rb!=1||a.generation>=b.generation)){selected=0;generation=a.generation;}
    else if(rb==1){ns_active=ns_pending;selected=1;generation=b.generation;}
    else if(ra==0&&(rb==0||rb==-2)){
        /* First 4.3 boot only: import old slots without writing them. */
        ra=load(2,&ns_active,&a);rb=load(3,&ns_pending,&b);
        if(ra==0&&rb==0){ra=load(4,&ns_active,&a);rb=load(5,&ns_pending,&b);}
        if(ra==1&&(rb!=1||a.generation>=b.generation)){}
        else if(rb==1)ns_active=ns_pending;
        else if(ra==0&&rb==0){nl_init(&ns_active);goto initialized;}
        else {nl_init(&ns_active);status="SD_INVALID_OR_INCOMPATIBLE";return;}
        if(!nl_validate(&ns_active)){status="SD_INVALID_OR_INCOMPATIBLE";return;}
    }
    else {nl_init(&ns_active);status="SD_INVALID_OR_INCOMPATIBLE";return;}
initialized:
    if(ns_active.boot_serial==UINT32_MAX){status="BOOT_SERIAL_EXHAUSTED";return;}
    available=1;ns_pending=ns_active;ns_pending.boot_serial++;
    ns_begin(); // committed before accepting enrollment in this boot
}
static void failed(int close_file) {
    if(close_file)f_close(&file);
    state=0;available=0;status="SD_WRITE_OR_VERIFY_FAILED";
}
void ns_poll(void) {
    UINT n;
    if(state==1) {
        if(f_open(&file,paths[target],FA_CREATE_ALWAYS|FA_WRITE)!=FR_OK){failed(0);return;}
        if(f_write(&file,&head,sizeof(head),&n)!=FR_OK||n!=sizeof(head)){failed(1);return;}
        state=2;
    }else if(state==2) {
        unsigned count=sizeof(ns_pending)-offset;if(count>512)count=512;
        if(f_write(&file,(const unsigned char *)&ns_pending+offset,count,&n)!=FR_OK||n!=count){failed(1);return;}
        offset+=count;if(offset==sizeof(ns_pending))state=3;
    }else if(state==3) {
        if(f_sync(&file)!=FR_OK){failed(1);return;}
        if(f_close(&file)!=FR_OK){failed(0);return;}
        if(f_open(&file,paths[target],FA_READ)!=FR_OK){failed(0);return;}
        header verify;
        if(f_size(&file)!=sizeof(head)+sizeof(ns_pending)||f_read(&file,&verify,sizeof(verify),&n)!=FR_OK||n!=sizeof(verify)||memcmp(&head,&verify,sizeof(head))){failed(1);return;}
        check_crc=crc_update(~0u,&head.generation,sizeof(head.generation));offset=0;state=4;
    }else if(state==4) {
        unsigned char buffer[512];unsigned count=sizeof(ns_pending)-offset;if(count>512)count=512;
        if(f_read(&file,buffer,count,&n)!=FR_OK||n!=count||memcmp(buffer,(const unsigned char *)&ns_pending+offset,count)){failed(1);return;}
        check_crc=crc_update(check_crc,buffer,count);offset+=count;
        if(offset==sizeof(ns_pending)) {
            if(~check_crc!=head.crc){failed(1);return;}
            if(f_close(&file)!=FR_OK){failed(0);return;}
            ns_active=ns_pending;generation=head.generation;selected=target;state=0;status="READY";
        }
    }
}
