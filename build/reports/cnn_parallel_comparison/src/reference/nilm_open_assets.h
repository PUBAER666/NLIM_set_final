#ifndef NILM_OPEN_ASSETS_H
#define NILM_OPEN_ASSETS_H
#include <stdint.h>
#define NO_ROWS 3720u
#define NO_TREES 160u
typedef struct {int32_t left,right,feature;double threshold,probability;} no_node;
extern const double no_x[3720*5],no_z[3720*64],no_mean[64],no_diag[64],no_radius[14];
extern const double no_multiplier,no_aux_threshold,no_tolerance,no_registered_radius;
extern const double no_r_low,no_r_high,no_d1_threshold,no_c_threshold;
extern const uint16_t no_labels[3720];
extern const uint32_t no_roots[160];
extern const no_node no_nodes[];
extern const double no_golden_x[15],no_golden_e[192],no_golden_logits[42],no_golden_probability[3];
extern const uint16_t no_golden_states[3];
#endif
