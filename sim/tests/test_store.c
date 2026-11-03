#include "nilm_store.h"
#include "ff.h"
#include "nilm_legacy.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static unsigned char disk[6][200000];static unsigned lengths[6],exists[6];
static int fail_write,fail_sync;
FRESULT f_mount(FATFS *f,const char *p,int m){(void)f;(void)p;(void)m;return 0;}
FRESULT f_open(FIL *f,const char *p,unsigned flags) {
    f->slot=strstr(p,"DORM1")?5:strstr(p,"DORM0")?4:strstr(p,"NILMV41")?3:strstr(p,"NILMV40")?2:strstr(p,"NILM431")?1:0;f->pos=0;
    if(flags&FA_CREATE_ALWAYS){exists[f->slot]=1;lengths[f->slot]=0;}
    return exists[f->slot]?FR_OK:FR_NO_FILE;
}
FRESULT f_close(FIL *f){(void)f;return 0;}
FRESULT f_sync(FIL *f){(void)f;return fail_sync?1:0;}
unsigned test_size(FIL *f){return lengths[f->slot];}
FRESULT f_read(FIL *f,void *p,unsigned count,UINT *n) {
    *n=lengths[f->slot]-f->pos;if(*n>count)*n=count;
    memcpy(p,disk[f->slot]+f->pos,*n);f->pos+=*n;return 0;
}
FRESULT f_write(FIL *f,const void *p,unsigned count,UINT *n) {
    *n=0;if(fail_write)return 1;
    assert(f->pos+count<sizeof(disk[0]));memcpy(disk[f->slot]+f->pos,p,count);f->pos+=count;
    if(f->pos>lengths[f->slot])lengths[f->slot]=f->pos;*n=count;return 0;
}
static void drain(void){unsigned n=0;while(ns_busy()&&n++<1000)ns_poll();assert(!ns_busy());}
static nl_bank bank;
typedef struct {uint32_t magic,schema,size,crc;uint64_t generation;} disk_header;
static uint32_t crc(uint32_t x,const void *data,unsigned n){const unsigned char *p=data;while(n--){x^=*p++;for(unsigned i=0;i<8;i++)x=(x>>1)^(0xedb88320u&(0u-(x&1)));}return x;}
static old_nl_bank old;
static void seed_legacy(unsigned slot,unsigned gen){
    old_nl_init(&old);old.support_scale=3;old.limit_w=1700;old.boot_serial=12;
    old.count=1;old_nl_profile *p=&old.profiles[0];strcpy(p->name,"old_device");p->count=p->next=8;
    p->selected[0]=0;p->selected[1]=1;p->selected[2]=2;p->radius5=p->radius64=1;
    for(unsigned k=0;k<8;k++){p->examples[k].key=k+1;p->examples[k].embedding[0]=(float)k;}
    disk_header h={0x314c494e,1,sizeof(old),0,gen};
    assert(old_nl_validate(&old));h.crc=~crc(crc(~0u,&h.generation,sizeof(h.generation)),&old,sizeof(old));
    memcpy(disk[slot],&h,sizeof(h));memcpy(disk[slot]+sizeof(h),&old,sizeof(old));lengths[slot]=sizeof(h)+sizeof(old);exists[slot]=1;
}

int main(void) {
    double v[5]={0};float emb[64]={0};
    nl_init(&bank);assert(nl_validate(&bank));
    assert(nl_enroll(&bank,"new",1,v,emb)==1);assert(nl_enroll(&bank,"new",1,v,emb)==-2);
    for(unsigned k=2;k<=8;k++){emb[0]=(float)k;assert(nl_enroll(&bank,"new",k,v,emb)==(int)k);}
    assert(nl_validate(&bank));
    v[0]=NAN;assert(nl_enroll(&bank,"bad",19,v,emb)<0);v[0]=0;
    ns_init();assert(ns_busy());drain();assert(ns_ready() && ns_active.boot_serial==1);
    ns_pending=ns_active;ns_pending.factor_set=1;ns_pending.factor=.5;assert(ns_begin()==0);
    assert(!ns_active.factor_set);drain();assert(ns_active.factor==.5);
    // Mid-file power loss: the old slot survives, boot serial advances safely.
    ns_pending=ns_active;ns_pending.factor=.7;assert(ns_begin()==0);ns_poll();ns_poll();
    ns_init();drain();assert(ns_ready() && ns_active.factor==.5 && ns_active.boot_serial==2);
    // Write and sync failure never publishes uncommitted data in RAM.
    ns_pending=ns_active;ns_pending.factor=.9;assert(ns_begin()==0);fail_write=1;ns_poll();
    assert(!ns_ready() && ns_active.factor==.5);fail_write=0;ns_init();drain();assert(ns_ready());
    ns_pending=ns_active;ns_pending.factor=.8;assert(ns_begin()==0);fail_sync=1;drain();assert(!ns_ready() && ns_active.factor==.5);
    fail_sync=0;ns_init();drain();assert(ns_ready()); // valid complete write may recover after reboot
    // Both corrupt: explicit unavailable, never silently create an empty library.
    disk[0][0]^=1;disk[1][0]^=1;ns_init();assert(!ns_ready() && !ns_busy());
    assert(!strcmp(ns_status(),"SD_INVALID_OR_INCOMPATIBLE"));
    memset(exists,0,sizeof(exists));seed_legacy(2,8);seed_legacy(3,9);
    unsigned before=crc(~0u,disk[3],lengths[3]);
    ns_init();drain();assert(ns_ready()&&ns_active.schema==5&&ns_active.support_scale==1&&ns_active.boot_serial==13);
    assert(ns_active.limit_w==1700&&ns_active.count==1&&ns_active.profiles[0].count==8);
    assert(crc(~0u,disk[3],lengths[3])==before); // old release can still roll back
    ns_pending=ns_active;ns_pending.factor=.7;assert(ns_begin()==0);drain();ns_init();drain();assert(ns_active.factor==.7);
    disk[0][0]^=1;disk[1][0]^=1;ns_init();assert(!ns_ready()); // never silently roll back to old slots
    memset(exists,0,2*sizeof(exists[0]));fail_write=1;ns_init();ns_poll();assert(!ns_ready());fail_write=0;
    ns_init();drain();assert(ns_ready()&&ns_active.support_scale==1&&ns_active.count==1);
    assert(crc(~0u,disk[3],lengths[3])==before);
    for(unsigned k=20;k<40;k++){v[0]=k;emb[0]=(float)k;assert(nl_enroll(&bank,"new",k,v,emb)>0);}
    assert(bank.profiles[0].count==12&&bank.profiles[0].next==28&&nl_validate(&bank));
    assert(nl_enroll(&bank,"new",21,v,emb)==-2);
    assert(nl_enroll(&bank,"kettle",100,v,emb)==-4);
    assert(nl_enroll(&bank,"unknown",100,v,emb)==-4);
    /* Durable delete/clear: only registered identities and their own rules go. */
    ns_pending=ns_active;assert(nl_enroll(&ns_pending,"second",1000,v,emb)>0);
    assert(nl_rule(&ns_pending,"old_device",1)==0);assert(nl_rule(&ns_pending,"Kettle",1)==0);
    ns_pending.factor=.42;ns_pending.factor_set=1;assert(ns_begin()==0);drain();
    assert(ns_active.count==2);unsigned revision=ns_active.revision;
    ns_pending=ns_active;assert(nl_delete(&ns_pending,"old_device")==0);assert(ns_begin()==0);
    assert(ns_active.count==2);fail_write=1;ns_poll();fail_write=0;
    assert(ns_active.count==2&&ns_active.revision==revision);
    ns_init();drain();assert(ns_ready()&&ns_active.count==2);
    ns_pending=ns_active;assert(nl_delete(&ns_pending,"old_device")==0);assert(ns_begin()==0);drain();
    assert(ns_active.count==1&&!strcmp(ns_active.profiles[0].name,"second"));
    assert(ns_active.rule_count==1&&!strcmp(ns_active.restricted[0],"Kettle"));
    assert(ns_active.limit_w==1700&&ns_active.factor==.42);
    ns_init();drain();assert(ns_active.count==1&&!strcmp(ns_active.profiles[0].name,"second"));
    ns_pending=ns_active;assert(nl_delete(&ns_pending,"missing")==-2);assert(nl_clear(&ns_pending)==0);
    assert(ns_begin()==0);ns_poll();ns_poll();ns_init();drain();assert(ns_active.count==1); /* interrupted clear */
    ns_pending=ns_active;assert(nl_clear(&ns_pending)==0);assert(ns_begin()==0);drain();
    ns_init();drain();assert(ns_ready()&&ns_active.count==0&&ns_active.rule_count==1);
    assert(ns_active.factor==.42&&ns_active.limit_w==1700); /* legacy slots must not resurrect refs */
    assert(nl_validate(&ns_active));
    puts("PASS durable single-delete, clear-all, reboot, interrupted clear, failed delete and unrelated settings retained");
    puts("PASS rolling 12 paired references, duplicate protection, reserved names; dual-slot CRC/write/sync/power-loss; legacy migration preserves rules/references, resets obsolete scale, leaves old files intact; migration retry and no stale rollback");
}
