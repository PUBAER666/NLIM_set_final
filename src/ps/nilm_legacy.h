#ifndef NILM_LEGACY_H
#define NILM_LEGACY_H
#include <stdint.h>
#define NL_PROFILES 32
#define OLD_NL_EVENTS 8
#define NL_RULES 32
typedef struct {uint64_t key;double vectors[5];float embedding[64];} old_nl_example;
typedef struct {char name[64];uint32_t count,next,selected[3];double radius5,radius64;old_nl_example examples[OLD_NL_EVENTS];} old_nl_profile;
typedef struct {
    uint32_t schema,model,preprocess,boot_serial,count,factor_set;
    double factor;
    double limit_w,confirm_s,clear_ratio,support_scale;
    uint32_t unknown_enabled,rule_count,revision;
    char restricted[NL_RULES][64];
    old_nl_profile profiles[NL_PROFILES];
} old_nl_bank;
enum {OLD_NL_NOT_READY,OLD_NL_NO_MATCH,OLD_NL_AMBIGUOUS,OLD_NL_CANDIDATE};
void old_nl_init(old_nl_bank *);
int old_nl_validate(const old_nl_bank *);
int old_nl_validate_legacy(const old_nl_bank *);
int old_nl_migrate_legacy(old_nl_bank *);
int old_nl_name_ok(const char *);
int old_nl_enroll(old_nl_bank *,const char *,uint64_t,const double [5],const float [64]);
int old_nl_rule(old_nl_bank *,const char *,unsigned);
#endif
