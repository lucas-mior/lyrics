#include "fftw.h"

#include <fftw3.h>
#include <stdio.h>

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
    int32 complex_count;

    fftw_real_plan_init_empty(plan);
    if (n_fft <= 0) {
        return false;
    }

    complex_count = n_fft/2 + 1;
    plan->real = fftwf_malloc((size_t)n_fft*sizeof(*plan->real));
    plan->complex = fftwf_malloc((size_t)complex_count*sizeof(fftwf_complex));
    if ((plan->real == 0) || (plan->complex == 0)) {
        fftw_real_plan_destroy(plan);
        return false;
    }

    plan->forward_plan = fftwf_plan_dft_r2c_1d(n_fft,
                                               plan->real,
                                               plan->complex,
                                               FFTW_ESTIMATE);
    plan->inverse_plan = fftwf_plan_dft_c2r_1d(n_fft,
                                               plan->complex,
                                               plan->real,
                                               FFTW_ESTIMATE);
    if ((plan->forward_plan == 0) || (plan->inverse_plan == 0)) {
        fftw_real_plan_destroy(plan);
        return false;
    }

    plan->n_fft = n_fft;

    return true;
}

void
fftw_real_plan_destroy(FftwRealPlan *plan) {
    if (plan->forward_plan != 0) {
        fftwf_destroy_plan(plan->forward_plan);
    }
    if (plan->inverse_plan != 0) {
        fftwf_destroy_plan(plan->inverse_plan);
    }
    if (plan->real != 0) {
        fftwf_free(plan->real);
    }
    if (plan->complex != 0) {
        fftwf_free(plan->complex);
    }

    fftw_real_plan_init_empty(plan);

    return;
}

#if TESTING_fftw

static int32
fftw_test_fail(char *name) {
    fprintf(stderr, "fftw test failed: %s\n", name);

    return 1;
}

int
main(void) {
    FftwRealPlan plan;

    fftw_real_plan_init_empty(&plan);
    if (plan.n_fft != 0) {
        return fftw_test_fail("empty n_fft");
    }
    if (!fftw_real_plan_init(&plan, 16)) {
        return fftw_test_fail("plan init");
    }
    if (plan.n_fft != 16) {
        return fftw_test_fail("plan n_fft");
    }
    fftw_real_plan_destroy(&plan);

    return 0;
}

#endif /* TESTING_fftw */
