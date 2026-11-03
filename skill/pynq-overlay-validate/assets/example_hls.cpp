#include <stdint.h>

// Independent demonstration IPs: AXI-Lite scalar ports only, no DMA/m_axi.
// Inspect exported component.xml/HWH to confirm register names and offsets.
void accel_add(uint32_t a, uint32_t b, uint32_t *result) {
#pragma HLS INTERFACE s_axilite port=a bundle=control offset=0x10
#pragma HLS INTERFACE s_axilite port=b bundle=control offset=0x18
#pragma HLS INTERFACE s_axilite port=result bundle=control offset=0x20
#pragma HLS INTERFACE s_axilite port=return bundle=control
    *result = a + b;
}

void accel_multiply(float x, float gain, float *result) {
#pragma HLS INTERFACE s_axilite port=x bundle=control offset=0x10
#pragma HLS INTERFACE s_axilite port=gain bundle=control offset=0x18
#pragma HLS INTERFACE s_axilite port=result bundle=control offset=0x20
#pragma HLS INTERFACE s_axilite port=return bundle=control
    *result = x * gain;
}
