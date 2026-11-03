#ifndef NILM_KNN_H
#define NILM_KNN_H
#include <stdint.h>
#define NK_DIM 16u
#define NK_K 7u
#define NK_ROWS 128u
/* Signed quantized features, squared Euclidean distance, uniform voting.
 * Feature scaling/definition must be supplied by the model group. */
typedef struct {
    uint32_t abi, version, preprocess_version, rows, dim, k, classes, min_votes;
    uint64_t max_nearest_distance;
    const int16_t *features; /* row-major; immutable until job complete */
    const uint16_t *labels;
} nk_model;
typedef struct {
    nk_model model;
    int16_t query[NK_DIM];
    uint64_t distance[NK_K];
    uint16_t label[NK_K];
    uint32_t row, used, winner, votes;
    int state; /* 0 unready, 1 running, 2 accepted, 3 rejected */
} nk_job;
int nk_start(nk_job *, const nk_model *, const int16_t *, uint32_t preprocess_version);
/* Processes at most four reference rows per call. */
int nk_poll(nk_job *);
#endif
