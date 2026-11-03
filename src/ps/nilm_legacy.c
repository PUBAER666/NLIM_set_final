#include "nilm_legacy.h"
#include "nilm_assets.h"
#include <math.h>
#include <string.h>
#include <float.h>
int old_nl_name_ok(const char *s) {
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

static int example_ok(const old_nl_example *e){
    if(!e->key)return 0;
    for(unsigned i=0;i<5;i++)if(!isfinite(e->vectors[i])||fabs(e->vectors[i])>1e6)return 0;
    for(unsigned i=0;i<64;i++)if(!isfinite(e->embedding[i])||fabsf(e->embedding[i])>1e6)return 0;
    return 1;
}
void old_nl_init(old_nl_bank *b){
    memset(b,0,sizeof(*b));b->schema=4;b->model=0x37ded17f;b->preprocess=2;
    b->confirm_s=1;b->clear_ratio=.9;b->support_scale=1;b->unknown_enabled=1;
}
static int validate(const old_nl_bank *b,unsigned schema){
    if(b->schema!=schema||b->model!=0x37ded17f||b->preprocess!=2||b->count>NL_PROFILES||b->rule_count>NL_RULES||b->unknown_enabled>1||b->factor_set>1)return 0;
    if(!isfinite(b->factor)||b->factor<0||b->factor>5||!isfinite(b->limit_w)||b->limit_w<0||b->limit_w>20000||!isfinite(b->confirm_s)||b->confirm_s<.04||b->confirm_s>30||!isfinite(b->clear_ratio)||b->clear_ratio<.5||b->clear_ratio>.99||!isfinite(b->support_scale)||b->support_scale<(schema==3?1:.25)||b->support_scale>20)return 0;
    for(unsigned i=0;i<b->rule_count;i++){
        if(!old_nl_name_ok(b->restricted[i]))return 0;
        for(unsigned j=0;j<i;j++)if(!strcmp(b->restricted[i],b->restricted[j]))return 0;
    }
    for(unsigned i=0;i<b->count;i++){
        const old_nl_profile *p=&b->profiles[i];
        if(!old_nl_name_ok(p->name)||p->count>8||!p->count||p->next>8)return 0;
        for(unsigned c=0;c<14;c++)if(!strcmp(p->name,ni_classes[c]))return 0;
        for(unsigned j=0;j<i;j++)if(!strcmp(p->name,b->profiles[j].name))return 0;
        for(unsigned j=0;j<p->count;j++)if(!example_ok(&p->examples[j]))return 0;
        if(p->count==8){
            if(!isfinite(p->radius5)||!isfinite(p->radius64)||p->radius5<=0||p->radius64<=0)return 0;
            for(unsigned j=0;j<3;j++)if(p->selected[j]>=8)return 0;
            if(p->selected[0]==p->selected[1]||p->selected[0]==p->selected[2]||p->selected[1]==p->selected[2])return 0;
        }
    }return 1;
}
int old_nl_validate(const old_nl_bank *b){return validate(b,4);}
int old_nl_validate_legacy(const old_nl_bank *b){return validate(b,3);}
int old_nl_migrate_legacy(old_nl_bank *b){
    if(!old_nl_validate_legacy(b))return 0;
    b->schema=4;b->support_scale=1;b->revision++;
    return old_nl_validate(b);
}
