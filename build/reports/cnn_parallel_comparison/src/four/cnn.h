#ifndef NILM_CNN_PL_H
#define NILM_CNN_PL_H
#include <stdint.h>
// ABI 2: 4096 physical-current float32 -> 14 logits followed by 64 CNN features.
// Model parameters are frozen into this IP; no normalization or softmax here.
#define NC_ABI 2u
#define NC_MODEL_TAG 0x37ded17fu
void nilm_cnn(const float *input, float *output, uint32_t request_abi,
              uint32_t request_model, uint32_t *status);
#endif
