#include "nilm_knn.h"
#include <string.h>
int nk_start(nk_job *j,const nk_model *m,const int16_t *q,uint32_t pre)
{
    uint32_t i;
    if(!j) return -1;
    memset(j,0,sizeof(*j));
    if(!m || !q || !m->features || !m->labels || m->abi!=0x10000u || !m->version ||
       !m->preprocess_version || pre!=m->preprocess_version || !m->rows || m->rows>NK_ROWS ||
       !m->dim || m->dim>NK_DIM || !m->k || m->k>NK_K || m->k>m->rows ||
       !m->classes || m->classes>16 || !m->min_votes || m->min_votes>m->k) return -1;
    for(i=0;i<m->rows;i++) if(m->labels[i]>=m->classes) return -1;
    j->model=*m; memcpy(j->query,q,m->dim*sizeof(*q)); j->state=1; return 0;
}
int nk_poll(nk_job *j)
{
    uint32_t budget=4,i,pos;
    const nk_model *m=&j->model;
    if(j->state!=1) return j->state;
    while(budget-- && j->row<m->rows) {
        uint64_t d=0;
        for(i=0;i<m->dim;i++) {
            int64_t delta=(int32_t)j->query[i]-(int32_t)m->features[j->row*m->dim+i];
            d+=(uint64_t)(delta*delta);
        }
        pos=0; while(pos<j->used && j->distance[pos]<=d) pos++;
        if(pos<m->k) {
            if(j->used<m->k) j->used++;
            for(i=j->used-1;i>pos;i--) { j->distance[i]=j->distance[i-1]; j->label[i]=j->label[i-1]; }
            j->distance[pos]=d; j->label[pos]=m->labels[j->row];
        }
        j->row++;
    }
    if(j->row==m->rows) {
        uint32_t votes[16]={0}; int tie=0;
        for(i=0;i<j->used;i++) votes[j->label[i]]++;
        for(i=0;i<m->classes;i++) {
            if(votes[i]>j->votes) { j->votes=votes[i]; j->winner=i; tie=0; }
            else if(votes[i]==j->votes && votes[i]) tie=1;
        }
        j->state=(!tie && j->votes>=m->min_votes && j->distance[0]<=m->max_nearest_distance)?2:3;
    }
    return j->state;
}
