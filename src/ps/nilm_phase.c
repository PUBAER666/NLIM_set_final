#include "nilm_phase.h"
#include <math.h>
#include <string.h>
static double median(double *a,unsigned n){
    for(unsigned i=1;i<n;i++){double x=a[i];unsigned j=i;while(j&&a[j-1]>x){a[j]=a[j-1];j--;}a[j]=x;}
    return n%2?a[n/2]:(a[n/2-1]+a[n/2])*.5;
}
static double lerp(const double *a,double p){unsigned k=(unsigned)p;double f=p-k;return a[k]*(1-f)+a[k+1]*f;}
void nph_begin(nph_job *j,const double v[6][4096],const double i[6][4096],double r[3][4096],unsigned on){
    memset(j,0,sizeof(*j));j->v=v;j->i=i;j->residual=r;j->on=on;
}
int nph_poll(nph_job *j){
    unsigned w=j->window,end=j->index+64;
    if(j->state==0){
        if(end>4095)end=4095;
        for(;j->index<end;j->index++){
            unsigned n=j->index;double a=j->v[w][n],b=j->v[w][n+1];
            if(a<=0&&b>0){if(j->count[w]==16)return -1;j->crossings[w][j->count[w]++]=n-a/(b-a);}
        }
        if(j->index==4095){if(j->count[w]<4)return -1;j->index=0;if(++j->window==6){j->window=0;j->state=1;}}return 0;
    }
    if(j->state==1){
        unsigned bg=w+(j->on?0:3);if(end>720)end=720;
        for(;j->index<end;j->index++){
            double v[16],i[16];unsigned count=0;
            for(unsigned c=0;c+1<j->count[bg];c++){
                double left=j->crossings[bg][c],period=j->crossings[bg][c+1]-left;
                if(period<400||period>600)continue;
                double at=left+(double)j->index/720*period;
                if(at<0||at>=4095)continue;
                v[count]=lerp(j->v[bg],at);i[count++]=lerp(j->i[bg],at);
            }
            if(count<4)return -1;
            j->vt[w][j->index]=median(v,count);j->it[w][j->index]=median(i,count);
        }
        if(j->index==720){j->index=0;if(++j->window==3){j->window=0;j->state=2;}}return 0;
    }
    if(j->state==2){
        if(end>720)end=720;
        for(;j->index<end;j->index++){
            double v[3],i[3];for(unsigned c=0;c<3;c++){v[c]=j->vt[c][j->index];i[c]=j->it[c][j->index];}
            j->background_v[j->index]=median(v,3);j->background_i[j->index]=median(i,3);
        }
        if(j->index==720){
            double bv=0;for(unsigned n=0;n<720;n++)bv+=j->background_v[n]*j->background_v[n];bv=sqrt(bv/720);if(bv<1)return -1;
            for(unsigned c=0;c<3;c++){unsigned target=c+(j->on?3:0);double sum=0;for(unsigned n=0;n<4096;n++)sum+=j->v[target][n]*j->v[target][n];j->scale[c]=fmax(.9,fmin(1.1,sqrt(sum/4096)/bv));}
            j->state=3;j->index=0;
        }return 0;
    }
    if(j->state==3){
        if(end>4096)end=4096;
        unsigned target=w+(j->on?3:0);const double *cross=j->crossings[target];unsigned count=j->count[target];
        for(;j->index<end;j->index++){
            double at=j->index,phase;unsigned seg=0;
            if(at<cross[0])phase=(at-cross[0])/(cross[1]-cross[0]);
            else if(at>=cross[count-1])phase=(at-cross[count-1])/(cross[count-1]-cross[count-2]);
            else {while(seg+1<count&&cross[seg+1]<=at)seg++;phase=(at-cross[seg])/(cross[seg+1]-cross[seg]);}
            phase-=floor(phase);double pos=phase*720;unsigned k=(unsigned)pos;double f=pos-k;
            double bg=(j->background_i[k]*(1-f)+j->background_i[(k+1)%720]*f)*j->scale[w];
            double r=j->i[target][j->index]-bg;j->residual[w][j->index]=r;j->mean[w]+=r/4096;
        }
        if(j->index==4096){j->index=0;if(++j->window==3){j->window=0;j->state=4;}}return 0;
    }
    if(j->state==4){
        if(end>4096)end=4096;
        for(;j->index<end;j->index++)j->residual[w][j->index]-=j->mean[w];
        if(j->index==4096){j->index=0;if(++j->window==3){j->state=5;return 1;}}return 0;
    }return j->state==5?1:-1;
}
