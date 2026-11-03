#include "nilm_learn.h"
#include "nilm_assets.h"
#include "nilm_open.h"
#include "nilm_legacy.h"
#include <math.h>
#include <string.h>
#include <float.h>
#include <ctype.h>
static int same_name(const char *a,const char *b){
    while(*a&&*b){if(tolower((unsigned char)*a)!=tolower((unsigned char)*b))return 0;a++;b++;}
    return *a==*b;
}
static int profile_name(const char *s){
    if(!nl_name_ok(s)||isspace((unsigned char)s[0])||isspace((unsigned char)s[strlen(s)-1]))return 0;
    if(same_name(s,"unknown")||same_name(s,"uncertain"))return 0;
    for(unsigned c=0;c<14;c++)if(same_name(s,ni_classes[c]))return 0;
    return 1;
}
int nl_name_ok(const char *s) {
    unsigned n;for(n=0;n<64 && s[n];n++)if((unsigned char)s[n]<32 || s[n]=='"' || s[n]=='\\')return 0;
    if(!n || n==64)return 0;
    for(unsigned i=0;i<n;) {
        unsigned c=(unsigned char)s[i++],code,extra;
        if(c<128){if(c==127)return 0;continue;}
        if(c>=0xc2&&c<=0xdf){code=c&31;extra=1;}
        else if(c>=0xe0&&c<=0xef){code=c&15;extra=2;}
        else if(c>=0xf0&&c<=0xf4){code=c&7;extra=3;}else return 0;
        if(i+extra>n)return 0;
        for(unsigned j=0;j<extra;j++){unsigned d=(unsigned char)s[i++];if((d&0xc0)!=0x80)return 0;code=(code<<6)|(d&63);}
        if((extra==2&&code<0x800)||(extra==3&&code<0x10000)||(code>=0xd800&&code<=0xdfff)||code>0x10ffff)return 0;
    }
    return 1;
}

static int example_ok(const nl_example *e){
    if(!e->key)return 0;
    for(unsigned i=0;i<5;i++)if(!isfinite(e->vectors[i])||fabs(e->vectors[i])>1e6)return 0;
    for(unsigned i=0;i<64;i++)if(!isfinite(e->embedding[i])||fabsf(e->embedding[i])>1e6)return 0;
    return 1;
}
void nl_init(nl_bank *b){
    memset(b,0,sizeof(*b));b->schema=5;b->model=0x3c3314d9;b->preprocess=2;
    b->confirm_s=1;b->clear_ratio=.9;b->support_scale=1;b->unknown_enabled=1;
}
static int validate(const nl_bank *b,unsigned schema){
    if(b->schema!=schema||b->model!=0x3c3314d9||b->preprocess!=2||b->count>NL_PROFILES||b->rule_count>NL_RULES||b->unknown_enabled>1||b->factor_set>1||b->support_scale!=1)return 0;
    if(!isfinite(b->factor)||b->factor<0||b->factor>5||!isfinite(b->limit_w)||b->limit_w<0||b->limit_w>20000||!isfinite(b->confirm_s)||b->confirm_s<.04||b->confirm_s>30||!isfinite(b->clear_ratio)||b->clear_ratio<.5||b->clear_ratio>.99||!isfinite(b->support_scale)||b->support_scale<(schema==3?1:.25)||b->support_scale>20)return 0;
    for(unsigned i=0;i<b->rule_count;i++){
        if(!nl_name_ok(b->restricted[i]))return 0;
        for(unsigned j=0;j<i;j++)if(!strcmp(b->restricted[i],b->restricted[j]))return 0;
    }
    for(unsigned i=0;i<b->count;i++){
        const nl_profile *p=&b->profiles[i];
        if(!profile_name(p->name)||p->count>12||!p->count||p->next<p->count||!p->last_key)return 0;
        for(unsigned c=0;c<14;c++)if(!strcmp(p->name,ni_classes[c]))return 0;
        for(unsigned j=0;j<i;j++)if(same_name(p->name,b->profiles[j].name))return 0;
        for(unsigned j=0;j<p->count;j++)if(!example_ok(&p->examples[j]))return 0;

    }return 1;
}
int nl_validate(const nl_bank *b){return validate(b,5);}
int nl_validate_legacy(const nl_bank *b){(void)b;return 0;}
int nl_migrate_legacy(nl_bank *b){(void)b;return 0;}
int nl_import_v4(nl_bank *b,const void *data,unsigned bytes){
    if(bytes!=sizeof(old_nl_bank))return 0;
    const old_nl_bank *old=data;
    if(!old_nl_validate(old)&&!old_nl_validate_legacy(old))return 0;
    nl_init(b);b->boot_serial=old->boot_serial;b->count=old->count;
    b->factor_set=old->factor_set;b->factor=old->factor;b->limit_w=old->limit_w;
    b->confirm_s=old->confirm_s;b->clear_ratio=old->clear_ratio;b->unknown_enabled=old->unknown_enabled;
    b->rule_count=old->rule_count;b->revision=old->revision+1;
    memcpy(b->restricted,old->restricted,sizeof(b->restricted));
    for(unsigned c=0;c<b->count;c++){
        nl_profile *p=&b->profiles[c];const old_nl_profile *o=&old->profiles[c];
        strcpy(p->name,o->name);p->count=p->next=o->count;
        for(unsigned k=0;k<p->count;k++){
            memcpy(&p->examples[k],&o->examples[k],sizeof(nl_example));
            if(p->examples[k].key>p->last_key)p->last_key=p->examples[k].key;
        }
    }
    return nl_validate(b);
}
int nl_enroll(nl_bank *b,const char *name,uint64_t key,const double v[5],const float e[64]){
    if(!nl_validate(b)||!nl_name_ok(name)||!key)return -1;
    if(!profile_name(name))return -4;
    nl_example x;memset(&x,0,sizeof(x));x.key=key;memcpy(x.vectors,v,sizeof(x.vectors));memcpy(x.embedding,e,sizeof(x.embedding));if(!example_ok(&x))return -1;
    if(v[1]*ni_scale[1]+ni_mean[1]<.025)return -1;
    unsigned slot=b->count;
    for(unsigned c=0;c<b->count;c++){
        if(same_name(b->profiles[c].name,name))slot=c;
        if(key<=b->profiles[c].last_key)return -2;
    }
    if(slot==b->count){if(b->count==NL_PROFILES)return -3;b->count++;strcpy(b->profiles[slot].name,name);}
    nl_profile *p=&b->profiles[slot];
    if(p->next==UINT32_MAX)return -1;
    no_reference rows[17];nl_example examples[17];unsigned selected[16];
    for(unsigned k=0;k<p->count;k++)examples[k]=p->examples[k];
    examples[p->count]=x;
    for(unsigned k=0;k<=p->count;k++){
        rows[k].id=14+slot;memcpy(rows[k].x,examples[k].vectors,sizeof(rows[k].x));
        memcpy(rows[k].embedding,examples[k].embedding,sizeof(rows[k].embedding));
    }
    int selected_count=no_select(rows,p->count+1,12,selected);if(selected_count<1)return -1;
    memset(p->examples,0,sizeof(p->examples));
    for(int k=0;k<selected_count;k++)p->examples[k]=examples[selected[k]];
    p->count=(unsigned)selected_count;p->next++;p->last_key=key;
    b->revision++;return (int)p->count;
}
int nl_rule(nl_bank *b,const char *name,unsigned add){
    if(!nl_name_ok(name)||!nl_validate(b)||add>1)return -1;
    unsigned i;for(i=0;i<b->rule_count;i++)if(!strcmp(name,b->restricted[i]))break;
    if(add){if(i<b->rule_count)return 0;if(i==NL_RULES)return -2;strcpy(b->restricted[b->rule_count++],name);}
    else {if(i==b->rule_count)return 0;memmove(b->restricted[i],b->restricted[i+1],(b->rule_count-i-1)*64);memset(b->restricted[--b->rule_count],0,64);}
    b->revision++;return 0;
}

/* Copy-on-write callers publish only after the existing CRC + fsync commit. */
int nl_delete(nl_bank *b,const char *name){
    if(!nl_validate(b)||!nl_name_ok(name))return -1;
    unsigned slot=0;while(slot<b->count&&!same_name(b->profiles[slot].name,name))slot++;
    if(slot==b->count)return -2;
    /* Remove this registered name's rule, leaving unrelated/base rules intact. */
    char removed[64];strcpy(removed,b->profiles[slot].name);
    if(nl_rule(b,removed,0)<0)return -1;
    memmove(&b->profiles[slot],&b->profiles[slot+1],(b->count-slot-1)*sizeof(nl_profile));
    memset(&b->profiles[--b->count],0,sizeof(nl_profile));b->revision++;
    return 0;
}
int nl_clear(nl_bank *b){
    if(!nl_validate(b))return -1;
    while(b->count)if(nl_delete(b,b->profiles[b->count-1].name)<0)return -1;
    return 0;
}
