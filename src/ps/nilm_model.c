#include "nilm_model.h"
#include <string.h>
#ifdef NI_PL_BACKEND
#include "nilm_pl.h"
#endif
int nm_init(nm_pipeline *p, uint32_t n)
{
    if (!p || !n || n>NM_MAX_WINDOW || n%NILM_STREAM_SAMPLES) return -1;
    memset(p,0,sizeof(*p)); p->target=n; return 0;
}
int nm_bind(nm_pipeline *p,unsigned slot,const nm_backend *b)
{
    if (!p || slot>1 || p->held || p->filled) return -1;
    /* Non-null backends must enter through the compatibility gate. */
    if (b) return -1;
    memset(&p->backend[slot],0,sizeof(p->backend[slot]));
    memset(&p->manifest[slot],0,sizeof(p->manifest[slot]));
    p->compatibility[slot]=NM_COMPAT_UNLOADED;
    memset(&p->result[slot],0,sizeof(p->result[slot]));
    return 0;
}
int nm_bind_checked(nm_pipeline *p,unsigned slot,const nm_backend *b,const nm_manifest *m)
{
    uint32_t c=NM_COMPAT_OK;
    if(!p || slot>1 || p->held || p->filled) return -1;
    if(!b || !m || !b->submit || !b->poll || !b->cancel || !m->model_version) c=NM_COMPAT_ASSETS;
    else if(m->abi!=NM_ABI) c=NM_COMPAT_ABI;
    else if(m->format!=1u) c=NM_COMPAT_INPUT;
    else if(m->window_samples!=p->target) c=NM_COMPAT_WINDOW;
    else if(m->preprocess_version!=2u) c=NM_COMPAT_PREPROCESS;
    else if(m->slot!=slot) c=NM_COMPAT_BACKEND;
#ifdef NI_PL_BACKEND
    else if(!m->requires_pl_accelerator || !np_available()) c=NM_COMPAT_BACKEND;
#else
    else if(m->requires_pl_accelerator) c=NM_COMPAT_BACKEND;
#endif
    memset(&p->backend[slot],0,sizeof(p->backend[slot]));
    memset(&p->manifest[slot],0,sizeof(p->manifest[slot]));
    memset(&p->result[slot],0,sizeof(p->result[slot]));
    p->compatibility[slot]=c;
    if(c!=NM_COMPAT_OK) return -1;
    p->backend[slot]=*b;p->manifest[slot]=*m;return 0;
}
void nm_abort(nm_pipeline *p)
{
    unsigned i;
    for(i=0;i<2;i++) {
        if(p->result[i].status==NM_BUSY && p->backend[i].cancel)
            p->backend[i].cancel(p->backend[i].context);
        p->result[i].status=NM_NOT_READY;
    }
    p->held=0; p->filled=0; p->have_last=0;
}
void nm_push(nm_pipeline *p,const nilm_stream_desc *d,const nilm_stream_sample *s)
{
    unsigned i;
    if(p->have_last && (d->epoch!=p->epoch || d->sequence!=p->last_seq+1 ||
                       d->first_id!=p->last_id+1u)) {
        p->discontinuities++; nm_abort(p);
    }
    p->have_last=1; p->epoch=d->epoch; p->last_seq=d->sequence; p->last_id=d->last_id;
    if(p->held) { p->skipped_blocks++; return; }
    if(!p->filled) {
        p->window.abi=NM_ABI; p->window.format=1;
        p->window.epoch=d->epoch; p->window.first_id=d->first_id;
        p->window.first_block=d->sequence;
    }
    memcpy(p->snapshot+p->filled,s,NILM_STREAM_SAMPLES*sizeof(*s));
    p->filled+=NILM_STREAM_SAMPLES;
    if(p->filled!=p->target) return;
    p->window.count=p->filled; p->window.samples=p->snapshot;
    p->window.last_id=d->last_id; p->window.last_block=d->sequence;
    p->window.job_id=++p->windows; p->held=1;
    for(i=0;i<2;i++) {
        memset(&p->result[i],0,sizeof(p->result[i]));
        p->result[i].job_id=p->window.job_id;
        if(p->backend[i].submit) {
            int status=p->backend[i].submit(p->backend[i].context,&p->window,&p->result[i]);
            p->result[i].status=(status>=NM_NOT_READY && status<=NM_ERROR)?(uint32_t)status:NM_ERROR;
        }
    }
}
void nm_poll(nm_pipeline *p)
{
    unsigned i; int busy=0;
    if(!p->held) return;
    for(i=0;i<2;i++) {
        if(p->result[i].status==NM_BUSY) {
            int status=p->backend[i].poll(p->backend[i].context,&p->result[i]);
            p->result[i].status=(status>=NM_NOT_READY && status<=NM_ERROR)?(uint32_t)status:NM_ERROR;
            if(p->result[i].status!=NM_BUSY && p->result[i].job_id!=p->window.job_id)
                p->result[i].status=NM_ERROR;
        }
        busy|=p->result[i].status==NM_BUSY;
    }
    if(!busy) { p->held=0; p->filled=0; }
}
