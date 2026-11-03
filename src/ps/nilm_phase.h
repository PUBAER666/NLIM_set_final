#ifndef NILM_PHASE_H
#define NILM_PHASE_H
/* Phase-template port of extract_event_change_from_detector_v1.py.
 * 720 phase bins, median across cycles and then 3 background windows.
 * No power fitting; background voltage scaling limited to [0.9,1.1]. */
typedef struct {
    double vt[3][720],it[3][720],background_v[720],background_i[720];
    double crossings[6][16],scale[3],mean[3];
    unsigned count[6],state,window,index,on;
    const double (*v)[4096],(*i)[4096];double (*residual)[4096];
} nph_job;
void nph_begin(nph_job *,const double [6][4096],const double [6][4096],double [3][4096],unsigned);
int nph_poll(nph_job *); /* 0 running, 1 complete, -1 invalid mains phase */
#endif
