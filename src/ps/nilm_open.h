#ifndef NILM_OPEN_H
#define NILM_OPEN_H
#include <stddef.h>
#include <stdint.h>
#define NO_REGISTRY_MARGIN 1.2
#define NO_REGISTERED_CLASSES 32
enum {NO_KNOWN,NO_UNKNOWN,NO_UNCERTAIN};
/* Immutable caller-owned registered references must live until polling completes.
 * Each row is paired; id >=14 is stable within the supplied registry snapshot. */
typedef struct {unsigned id;double x[5];float embedding[64];} no_reference;
typedef struct {
    double x[5],z[64],dist5[14],dist64[14],paired_base,paired_registered,registered_distance;
    float input[69],logits[14];
    double probability,score,distance_score,auxiliary_score,candidate_score,confidence,d1,d2;
    unsigned phase,index,knn,cnn,label,closed_label,matched,state,called,registered_candidate,bypassed;
    unsigned registered_count;const no_reference *registered_rows;
    double registered_by_class[NO_REGISTERED_CLASSES],registry_second,registry_margin;
    const char *registry_reason;
    const char *reason;
} no_job;
void no_begin(no_job *,const double standardized5[5],const float embedding[64],const float logits[14],const no_reference *,unsigned);
int no_poll(no_job *); /* bounded batches: 16 reference rows or 4 trees per call */
void no_transform(const float [64],double [64]);
int no_selftest(void);
/* Representative selector: at most 32 merged input rows, up to 16 output rows.
 * Coordinates match the handoff's paired standardized 5D/transformed 64D space. */
int no_select(const no_reference *,unsigned count,unsigned maximum,unsigned selected[16]);
#endif
