#ifndef NILM_LEARN_H
#define NILM_LEARN_H
#include <stdint.h>
#define NL_PROFILES 32
#define NL_EVENTS 16
#define NL_RULES 32
typedef struct {uint64_t key;double vectors[5];float embedding[64];} nl_example;
typedef struct {char name[64];uint32_t count,next,selected[3];double radius5,radius64;nl_example examples[NL_EVENTS];uint64_t last_key;} nl_profile;
typedef struct {
    uint32_t schema,model,preprocess,boot_serial,count,factor_set;
    double factor;
    double limit_w,confirm_s,clear_ratio,support_scale;
    uint32_t unknown_enabled,rule_count,revision;
    char restricted[NL_RULES][64];
    nl_profile profiles[NL_PROFILES];
} nl_bank;
enum {NL_NOT_READY,NL_NO_MATCH,NL_AMBIGUOUS,NL_CANDIDATE};
void nl_init(nl_bank *);
int nl_validate(const nl_bank *);
int nl_validate_legacy(const nl_bank *);
int nl_migrate_legacy(nl_bank *);
int nl_import_v4(nl_bank *,const void *,unsigned);
int nl_name_ok(const char *);
int nl_enroll(nl_bank *,const char *,uint64_t,const double [5],const float [64]);
int nl_delete(nl_bank *,const char *);
int nl_clear(nl_bank *);
int nl_rule(nl_bank *,const char *,unsigned);
#endif
