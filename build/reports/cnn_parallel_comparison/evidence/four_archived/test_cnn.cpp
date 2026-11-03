#include "cnn.h"
extern "C" {
#include "nilm_infer.h"
}
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cassert>
#ifndef PL_CASE
#define PL_CASE 0
#endif
static ni_engine reference;
static float input[4096], output[78];
int main() {
    uint32_t status=99;
    for(int c=0;c<14;c++)output[c]=123;
    nilm_cnn(input,output,0,NC_MODEL_TAG,&status);
    assert(status==2 && output[0]==123);
    nilm_cnn(input,output,NC_ABI,0,&status);
    assert(status==2 && output[0]==123);
    float worst=0;
#ifdef PL_COSIM
    const unsigned tests=1;
#else
    const unsigned tests=24;
#endif
    for(unsigned case_index=0;case_index<tests;case_index++) {
        unsigned test=case_index+PL_CASE;
        unsigned rng=123+test;
        for(unsigned n=0;n<4096;n++) {
            rng=1664525*rng+1013904223;
            float noise=((rng>>8)/16777216.0f-0.5f);
            float phase=6.28318530718f*n/(512.0f+test);
            input[n]=test==0?0:test==1?(n==2048?10:0):
                test==2?1:(0.02f*test)*(std::sin(phase)+0.2f*std::sin(3*phase))+noise*0.01f;
        }
        memset(&reference,0,sizeof(reference));
        assert(ni_start_cnn(&reference,input)==0);
        nm_result result={};int state=NM_BUSY;unsigned guard=0;
        while(state==NM_BUSY && guard++<10000)state=ni_poll(&reference,&result);
        assert(state==NM_DONE);
        nilm_cnn(input,output,NC_ABI,NC_MODEL_TAG,&status);
        assert(status==0);
        for(int c=0;c<78;c++) {
            float expected=c<14?reference.logits[c]:reference.embedding[c-14];
            float err=std::fabs(output[c]-expected);
            if(err>worst)worst=err;
            if(!std::isfinite(output[c]) || err>1e-4f) {
                std::printf("FAIL test=%u class=%d expected=%.9g got=%.9g\n",test,c,expected,output[c]);return 1;
            }
        }
    }
    std::printf("PASS %u inputs x14 logits +64 features, ABI/model rejection; worst_abs=%.9g\n",tests,worst);
}

