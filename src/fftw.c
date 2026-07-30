#include "fftw.h"

void
fftw_real_plan_init_empty(FftwRealPlan *plan) {
    plan->n_fft = 0;

    plan->real = 0;
    plan->complex = 0;
    plan->forward_plan = 0;
    plan->inverse_plan = 0;

    return;
}

bool
fftw_real_plan_init(FftwRealPlan *plan, int32 n_fft) {
    fftw_real_plan_init_empty(plan);

    plan->n_fft = n_fft;

    return false;
}

void
fftw_real_plan_destroy(FftwRealPlan *plan) {
    fftw_real_plan_init_empty(plan);

    return;
}

#if TESTING_fftw

int
main(void) {
    FftwRealPlan plan;

    fftw_real_plan_init_empty(&plan);
    fftw_real_plan_destroy(&plan);

    return 0;
}

#endif /* TESTING_fftw */
