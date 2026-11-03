#include "cnn.h"
#include "weights.h"

// Matched single-output-channel baseline. Eight positions still interleave
// each floating-point recurrence; tap order and numerical precision unchanged.
template<int L, int IC, int OC, int K, bool POOL>
static void layer(const float input[IC*L], float output[OC*L/2],
                  float embedding[64], const float weight[OC][IC*K],
                  const float bias[OC], const float alpha[OC], const float beta[OC])
{
#pragma HLS INLINE off
#pragma HLS ARRAY_PARTITION variable=weight cyclic factor=1 dim=1
    for (int group=0; group<OC; group+=1) {
        float sum[1]={0}, peak[1]={0};
#pragma HLS ARRAY_PARTITION variable=sum complete
#pragma HLS ARRAY_PARTITION variable=peak complete
        for (int pos=0; pos<L; pos+=8) {
            float acc[1][8];
#pragma HLS ARRAY_PARTITION variable=acc complete dim=0
            for (int p=0;p<8;p++) {
#pragma HLS PIPELINE II=1
                for(int o=0;o<1;o++) {
#pragma HLS UNROLL
                    acc[o][p]=bias[group+o];
                }
            }
            for(int ic=0;ic<IC;ic++) for(int k=0;k<K;k++) {
                for(int p=0;p<8;p++) {
#pragma HLS PIPELINE II=1
                    int at=pos+p+k-K/2;
                    // Skipping padding MACs also preserves signed-zero behavior.
                    if(at>=0 && at<L) {
                        float x=input[ic*L+at];
                        for(int o=0;o<1;o++) {
#pragma HLS UNROLL
                            acc[o][p]+=x*weight[group+o][ic*K+k];
                        }
                    }
                }
            }
            for(int o=0;o<1;o++) {
                float previous=0;
                for(int p=0;p<8;p++) {
                    float y=acc[o][p]*alpha[group+o]+beta[group+o];
                    y=y>0?y:0;
                    if(POOL) {
                        if(p&1) output[(group+o)*(L/2)+(pos+p)/2]=previous>y?previous:y;
                        previous=y;
                    } else {
                        sum[o]+=y/1024.0f;
                        if(y>peak[o])peak[o]=y;
                    }
                }
            }
        }
        if(!POOL) for(int o=0;o<1;o++) {
            embedding[group+o]=sum[o];embedding[group+o+32]=peak[o];
        }
    }
}

void nilm_cnn(const float *input, float *output, uint32_t request_abi,
              uint32_t request_model, uint32_t *status)
{
#pragma HLS INTERFACE m_axi port=input offset=slave bundle=memory depth=4096 max_read_burst_length=64
#pragma HLS INTERFACE m_axi port=output offset=slave bundle=memory depth=78 max_write_burst_length=16
#pragma HLS INTERFACE s_axilite port=input bundle=control
#pragma HLS INTERFACE s_axilite port=output bundle=control
#pragma HLS INTERFACE s_axilite port=request_abi bundle=control
#pragma HLS INTERFACE s_axilite port=request_model bundle=control
#pragma HLS INTERFACE s_axilite port=status bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
    *status=1;
    if(request_abi!=NC_ABI || request_model!=NC_MODEL_TAG){*status=2;return;}
    float x[4096], pool1[16384], pool2[16384], embedding[64], unused[1];
#pragma HLS BIND_STORAGE variable=x type=ram_2p impl=bram
#pragma HLS BIND_STORAGE variable=pool1 type=ram_2p impl=bram
#pragma HLS BIND_STORAGE variable=pool2 type=ram_2p impl=bram
    for(int n=0;n<4096;n++) {
#pragma HLS PIPELINE II=1
        x[n]=input[n];
    }
    layer<4096,1,8,9,true>(x,pool1,embedding,w1,b1,a1,c1);
    layer<2048,8,16,7,true>(pool1,pool2,embedding,w2,b2,a2,c2);
    layer<1024,16,32,5,false>(pool2,unused,embedding,w3,b3,a3,c3);
    for(int c=0;c<14;c++) {
        float value=fc_b[c];
        for(int n=0;n<64;n++)value+=embedding[n]*fc_w[c*64+n];
        output[c]=value;
    }
    for(int n=0;n<64;n++)output[14+n]=embedding[n];
    *status=0;
}
