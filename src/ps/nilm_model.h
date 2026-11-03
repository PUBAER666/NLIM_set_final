#ifndef NILM_MODEL_H
#define NILM_MODEL_H
#include "nilm_stream.h"
#define NM_ABI 0x00010000u
#define NM_MAX_WINDOW 8192u
#define NM_CLASSES 16u
#define NM_PREPROCESS_RAW1 0u
enum { NM_COMPAT_UNLOADED, NM_COMPAT_OK, NM_COMPAT_ABI, NM_COMPAT_INPUT,
       NM_COMPAT_WINDOW, NM_COMPAT_PREPROCESS, NM_COMPAT_BACKEND, NM_COMPAT_ASSETS };
typedef struct {
    uint32_t abi, format, window_samples, preprocess_version, model_version;
    uint32_t slot, requires_pl_accelerator;
} nm_manifest;
enum { NM_NOT_READY, NM_BUSY, NM_DONE, NM_REJECTED, NM_ERROR };
/* RAW1 snapshot, immutable until backend poll leaves BUSY. No physical units. */
typedef struct {
    uint32_t abi, format, count, epoch, first_id, last_id;
    uint64_t first_block, last_block, job_id;
    const nilm_stream_sample *samples;
} nm_window;
typedef struct {
    uint32_t status, model_version, preprocess_version, label;
    uint64_t job_id;
    /* A vote or score is backend-specific, not a calibrated probability. */
    uint32_t vote_count;
} nm_result;
typedef struct {
    void *context;
    /* All callbacks bounded/nonblocking; submit must reject missing assets.
     * Terminal poll/cancel guarantees no further access to the window.
     * A hardware backend must drain DMA before returning from cancel. */
    int (*submit)(void *, const nm_window *, nm_result *);
    int (*poll)(void *, nm_result *);
    void (*cancel)(void *);
} nm_backend;
typedef struct {
    nilm_stream_sample snapshot[NM_MAX_WINDOW];
    nm_window window;
    nm_backend backend[2]; /* 0: PS KNN-CNN cascade; 1: reserved, unbound */
    nm_result result[2];
    nm_manifest manifest[2];
    uint32_t compatibility[2];
    uint32_t target, filled, epoch, last_id;
    uint64_t last_seq, windows, discontinuities, skipped_blocks;
    int have_last, held;
} nm_pipeline;
int nm_init(nm_pipeline *, uint32_t window_samples);
/* Bind only when no window is being assembled or held; backend owns assets. */
int nm_bind(nm_pipeline *, unsigned slot, const nm_backend *);
/* Loaded assets must already be validated by their loader. Current firmware
 * supports RAW1 input only, and has no PL inference accelerator. */
int nm_bind_checked(nm_pipeline *, unsigned, const nm_backend *, const nm_manifest *);
void nm_push(nm_pipeline *, const nilm_stream_desc *, const nilm_stream_sample *);
void nm_poll(nm_pipeline *);
void nm_abort(nm_pipeline *);
#endif
