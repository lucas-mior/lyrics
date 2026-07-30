#if !defined(STFT_H)
#define STFT_H

#include "../cbase/primitives.h"

#include <stdbool.h>

typedef struct StftPlan {
    int32 n_fft;
    int32 hop;

    float *window;
} StftPlan;

void stft_plan_init_empty(StftPlan *plan);
bool stft_plan_init(StftPlan *plan, int32 n_fft, int32 hop);
void stft_plan_destroy(StftPlan *plan);

#endif /* STFT_H */
