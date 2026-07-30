#include "stft.h"

void
stft_plan_init_empty(StftPlan *plan) {
    plan->n_fft = 0;
    plan->hop = 0;

    plan->window = 0;

    return;
}

bool
stft_plan_init(StftPlan *plan, int32 n_fft, int32 hop) {
    stft_plan_init_empty(plan);

    plan->n_fft = n_fft;
    plan->hop = hop;

    return false;
}

void
stft_plan_destroy(StftPlan *plan) {
    stft_plan_init_empty(plan);

    return;
}

#if TESTING_stft

int
main(void) {
    StftPlan plan;

    stft_plan_init_empty(&plan);
    stft_plan_destroy(&plan);

    return 0;
}

#endif /* TESTING_stft */
