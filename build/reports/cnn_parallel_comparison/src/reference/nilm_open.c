/* Frozen candidate 20261101. No network, storage, fitting or waveform processing. */
#include "nilm_open.h"
#include "nilm_open_assets.h"
#include <math.h>
#include <float.h>
#include <string.h>
static void invalid(no_job *j,const char *why){j->state=NO_UNCERTAIN;j->reason=why;j->phase=4;}
void no_transform(const float e[64],double z[64]){for(unsigned n=0;n<64;n++)z[n]=((double)e[n]-no_mean[n])*no_diag[n];}
static double squared(const double *a,const double *b,unsigned width){double d=0;for(unsigned n=0;n<width;n++){double v=a[n]-b[n];d+=v*v;}return d;}
void no_begin(no_job *j,const double x[5],const float e[64],const float logits[14],const no_reference *refs,unsigned count){
    memset(j,0,sizeof(*j));j->state=NO_UNCERTAIN;j->reason="PENDING";j->registered_candidate=~0u;j->registry_reason="NOT_EVALUATED";j->registry_second=DBL_MAX;
    for(unsigned c=0;c<NO_REGISTERED_CLASSES;c++)j->registered_by_class[c]=DBL_MAX;
    j->paired_base=j->paired_registered=j->registered_distance=DBL_MAX;
    for(unsigned c=0;c<14;c++)j->dist5[c]=j->dist64[c]=DBL_MAX;
    if(!x||!e||!logits||(count&&!refs)||count>512){invalid(j,"INVALID_INPUT");return;}
    j->registered_rows=refs;j->registered_count=count;
    memcpy(j->x,x,sizeof(j->x));memcpy(j->logits,logits,sizeof(j->logits));
    no_transform(e,j->z);
    for(unsigned n=0;n<69;n++){
        double v=n<5?x[n]:e[n-5];j->input[n]=(float)v;
        if(!isfinite(v)||!isfinite(j->input[n])){invalid(j,"INVALID_FEATURES");return;}
    }
    for(unsigned n=0;n<64;n++)if(!isfinite(j->z[n])){invalid(j,"EMBEDDING_NUMERIC_RANGE");return;}
    for(unsigned n=0;n<14;n++)if(!isfinite(logits[n])){invalid(j,"INVALID_LOGITS");return;}
}
static void classify(no_job *j){
    /* knn is recorded while scanning rows: preserve original first-row tie order. */
    j->d1=sqrt(j->dist5[j->knn]);j->d2=DBL_MAX;
    for(unsigned c=0;c<14;c++)if(c!=j->knn&&j->dist5[c]<j->d2)j->d2=j->dist5[c];
    j->d2=sqrt(j->d2);double ratio=j->d1>1e-12?j->d2/j->d1:DBL_MAX;
    j->called=ratio<=no_r_low||(j->d1>=no_d1_threshold&&ratio<=no_r_high);
    j->cnn=0;for(unsigned c=1;c<14;c++)if(j->logits[c]>j->logits[j->cnn])j->cnn=c;
    float sum=0;for(unsigned c=0;c<14;c++)sum+=expf(j->logits[c]-j->logits[j->cnn]);
    j->confidence=1.0f/sum;
    j->label=j->called&&j->cnn!=j->knn&&j->confidence>=no_c_threshold?j->cnn:j->knn;
    j->closed_label=j->label;
    j->distance_score=DBL_MAX;
    for(unsigned c=0;c<14;c++){
        double score=no_multiplier*sqrt(j->dist64[c])/no_radius[c];
        if(score<j->distance_score){j->distance_score=score;j->matched=c;}
    }
    j->candidate_score=no_multiplier*sqrt(j->dist64[j->label])/no_radius[j->label];
    j->auxiliary_score=j->probability/no_aux_threshold;j->score=fmax(j->distance_score,j->auxiliary_score);
    j->state=j->score>1+no_tolerance?NO_UNKNOWN:NO_KNOWN;
    j->reason=j->state==NO_KNOWN?"WITHIN_EMBEDDING_SUPPORT":j->auxiliary_score>1+no_tolerance?"AUXILIARY_UNKNOWN_EXPOSURE":"OUTSIDE_EMBEDDING_SUPPORT";
}
int no_poll(no_job *j){
    if(j->phase==4)return 1;
    if(j->phase==0){
        unsigned end=j->index+16;if(end>NO_ROWS)end=NO_ROWS;
        for(;j->index<end;j->index++){
            unsigned r=j->index,c=no_labels[r];double d5=squared(j->x,no_x+r*5,5),d64=squared(j->z,no_z+r*64,64);
            if(!isfinite(d5)||!isfinite(d64)){invalid(j,"EMBEDDING_NUMERIC_RANGE");return 1;}
            if(d5<j->dist5[j->knn])j->knn=c;
            if(d5<j->dist5[c])j->dist5[c]=d5;
            if(d64<j->dist64[c])j->dist64[c]=d64;
            double paired=d5/5+d64/64;if(paired<j->paired_base)j->paired_base=paired;
        }
        if(j->index==NO_ROWS){j->index=0;j->phase=1;}return 0;
    }
    if(j->phase==1){
        unsigned end=j->index+4;if(end>NO_TREES)end=NO_TREES;
        for(;j->index<end;j->index++){
            int at=(int)no_roots[j->index];unsigned guard=0;
            while(no_nodes[at].left!=-1){
                const no_node *n=&no_nodes[at];
                at=j->input[n->feature]<=n->threshold?n->left:n->right;
                if(++guard>128){invalid(j,"FOREST_STRUCTURE");return 1;}
            }
            j->probability+=no_nodes[at].probability/NO_TREES;
        }
        if(j->index==NO_TREES){
            classify(j);j->registry_reason=j->state!=NO_UNKNOWN?"BASE_KNOWN_PROTECTED":j->registered_count?"SCANNING":"NO_REGISTERED_REFERENCES";j->index=0;
            j->phase=j->state==NO_UNKNOWN&&j->registered_count?2:4;
        }return j->phase==4;
    }
    /* Registry never overrides base known/uncertain decisions. */
    if(j->phase==2){
        unsigned end=j->index+16;if(end>j->registered_count)end=j->registered_count;
        for(;j->index<end;j->index++){
            const no_reference *r=j->registered_rows+j->index;double z[64];no_transform(r->embedding,z);
            double d5=squared(j->x,r->x,5),d64=squared(j->z,z,64),paired=d5/5+d64/64;
            if(!isfinite(paired)||r->id<14||r->id>=14+NO_REGISTERED_CLASSES){invalid(j,"INVALID_REGISTRY");return 1;}
            if(paired<j->registered_by_class[r->id-14])j->registered_by_class[r->id-14]=paired;
            if(paired<j->paired_registered){j->paired_registered=paired;j->registered_candidate=r->id;}
        }
        if(j->index==j->registered_count){
            /* Base UNKNOWN is not a veto on a bounded registered candidate.
               Compete only against other registered identities, not base references. */
            for(unsigned c=0;c<NO_REGISTERED_CLASSES;c++)if(c+14!=j->registered_candidate&&j->registered_by_class[c]<j->registry_second)j->registry_second=j->registered_by_class[c];
            j->registry_margin=j->registry_second==DBL_MAX?DBL_MAX:
                j->paired_registered>1e-24?sqrt(j->registry_second/j->paired_registered):j->registry_second<=1e-24?1:DBL_MAX;
            j->index=0;j->phase=3;
        }return 0;
    }
    unsigned end=j->index+16;if(end>j->registered_count)end=j->registered_count;
    for(;j->index<end;j->index++){
        const no_reference *r=j->registered_rows+j->index;double z[64];no_transform(r->embedding,z);
        double d64=squared(j->z,z,64),score=no_multiplier*sqrt(d64)/no_registered_radius;
        if(score<j->distance_score){j->distance_score=score;j->matched=r->id;}
        if(r->id==j->registered_candidate&&d64<j->registered_distance)j->registered_distance=d64;
    }
    if(j->index==j->registered_count){
        j->candidate_score=no_multiplier*sqrt(j->registered_distance)/no_registered_radius;
        int radius_ok=j->candidate_score<=1+no_tolerance;
        int separated=j->registry_margin>=NO_REGISTRY_MARGIN;
        j->bypassed=radius_ok&&separated;
        j->registry_reason=!radius_ok?"REGISTERED_OUTSIDE_RADIUS":!separated?"REGISTERED_CLASS_AMBIGUOUS":"REGISTERED_ACCEPTED";
        if(j->bypassed)j->auxiliary_score=0;
        j->score=j->bypassed?j->candidate_score:fmax(j->candidate_score,separated?j->auxiliary_score:NO_REGISTRY_MARGIN/fmax(j->registry_margin,1e-12));
        j->state=j->bypassed?NO_KNOWN:NO_UNKNOWN;
        j->label=j->registered_candidate;
        j->reason=j->state==NO_KNOWN?"REGISTERED_ACCEPTED":j->registry_reason;
        j->phase=4;return 1;
    }return 0;
}
int no_select(const no_reference *r,unsigned count,unsigned maximum,unsigned selected[16]){
    if(!r||!selected||!count||count>32||!maximum||maximum>16)return -1;
    double space[32][69],median[69],nearest[32];unsigned unique[32],n=0;
    for(unsigned k=0;k<count;k++){
        double z[64];no_transform(r[k].embedding,z);
        for(unsigned c=0;c<69;c++){
            space[n][c]=c<5?r[k].x[c]/sqrt(5.):z[c-5]/8;
            if(!isfinite(space[n][c]))return -1;
        }
        unsigned duplicate=0;for(unsigned p=0;p<n;p++)if(squared(space[n],space[p],69)==0){duplicate=1;break;}
        if(!duplicate)unique[n++]=k;
    }
    for(unsigned c=0;c<69;c++){
        double sorted[32];for(unsigned k=0;k<n;k++){
            unsigned p=k;while(p&&sorted[p-1]>space[k][c]){sorted[p]=sorted[p-1];p--;}sorted[p]=space[k][c];
        }
        median[c]=n%2?sorted[n/2]:(sorted[n/2-1]+sorted[n/2])/2;
    }
    unsigned first=0;double best=DBL_MAX;
    for(unsigned k=0;k<n;k++){double d=squared(space[k],median,69);if(d<best){best=d;first=k;}}
    unsigned chosen[16];chosen[0]=first;
    for(unsigned k=0;k<n;k++)nearest[k]=squared(space[k],space[first],69);
    unsigned total=n<maximum?n:maximum;
    for(unsigned k=1;k<total;k++){
        for(unsigned p=0;p<k;p++)nearest[chosen[p]]=-1;
        unsigned at=0;for(unsigned p=1;p<n;p++)if(nearest[p]>nearest[at])at=p;
        chosen[k]=at;for(unsigned p=0;p<n;p++)nearest[p]=fmin(nearest[p],squared(space[p],space[at],69));
    }
    for(unsigned k=0;k<total;k++)selected[k]=unique[chosen[k]];
    return (int)total;
}
int no_selftest(void){
    for(unsigned k=0;k<3;k++){
        float e[64],logits[14];no_job j;
        for(unsigned n=0;n<64;n++)e[n]=(float)no_golden_e[k*64+n];
        for(unsigned n=0;n<14;n++)logits[n]=(float)no_golden_logits[k*14+n];
        no_begin(&j,no_golden_x+k*5,e,logits,0,0);
        unsigned calls=0;while(!no_poll(&j))if(++calls>1000)return 0;
        if(j.state!=no_golden_states[k]||!isfinite(j.probability)||fabs(j.probability-no_golden_probability[k])>1e-10)return 0;
    }
    return 1;
}
