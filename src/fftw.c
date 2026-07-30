#include "fftw.h"

#include "../cbase/base_macros.h"

#if !defined(TESTING_fftw)
#define TESTING_fftw 0
#endif

#include <fftw3.h>
#include <math.h>
#include <stdio.h>

void
fftw_real_plan_init_empty(FftwRealPlan *plan) {
    plan->n_fft = 0;
    plan->complex_count = 0;

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
    plan->real = fftwf_malloc((size_t)(n_fft*SIZEOF(*plan->real)));
    plan->complex = fftwf_malloc((size_t)(complex_count*SIZEOF(fftwf_complex)));
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
    plan->complex_count = complex_count;

    return true;
}

bool
fftw_real_forward(
    FftwRealPlan *plan,
    float *input,
    float *output_real,
    float *output_imag
) {
    fftwf_complex *complex;

    if ((plan == 0) || (input == 0) || (output_real == 0)
        || (output_imag == 0)) {
        return false;
    }
    if ((plan->n_fft <= 0) || (plan->complex_count <= 0)) {
        return false;
    }
    if ((plan->real == 0) || (plan->complex == 0)
        || (plan->forward_plan == 0)) {
        return false;
    }

    for (int32 i = 0; i < plan->n_fft; i += 1) {
        plan->real[i] = input[i];
    }

    fftwf_execute((fftwf_plan)plan->forward_plan);

    complex = (fftwf_complex *)plan->complex;
    for (int32 i = 0; i < plan->complex_count; i += 1) {
        output_real[i] = complex[i][0];
        output_imag[i] = complex[i][1];
    }

    return true;
}

bool
fftw_real_inverse(
    FftwRealPlan *plan,
    float *input_real,
    float *input_imag,
    float *output
) {
    fftwf_complex *complex;
    float scale;

    if ((plan == 0) || (input_real == 0) || (input_imag == 0)
        || (output == 0)) {
        return false;
    }
    if ((plan->n_fft <= 0) || (plan->complex_count <= 0)) {
        return false;
    }
    if ((plan->real == 0) || (plan->complex == 0)
        || (plan->inverse_plan == 0)) {
        return false;
    }

    complex = (fftwf_complex *)plan->complex;
    for (int32 i = 0; i < plan->complex_count; i += 1) {
        complex[i][0] = input_real[i];
        complex[i][1] = input_imag[i];
    }

    fftwf_execute((fftwf_plan)plan->inverse_plan);

    scale = 1.0f/(float)plan->n_fft;
    for (int32 i = 0; i < plan->n_fft; i += 1) {
        output[i] = plan->real[i]*scale;
    }

    return true;
}

void
fftw_real_plan_destroy(FftwRealPlan *plan) {
    if (plan->forward_plan != 0) {
        fftwf_destroy_plan((fftwf_plan)plan->forward_plan);
    }
    if (plan->inverse_plan != 0) {
        fftwf_destroy_plan((fftwf_plan)plan->inverse_plan);
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

static bool
fftw_float_close(float a, float b) {
    return fabsf(a - b) < 0.0001f;
}

int
main(void) {
    FftwRealPlan plan;
    float impulse[8];
    float output[8];
    float real[5];
    float imag[5];

    fftw_real_plan_init_empty(&plan);
    if (plan.n_fft != 0) {
        return fftw_test_fail("empty n_fft");
    }
    if (plan.complex_count != 0) {
        return fftw_test_fail("empty complex count");
    }
    if (fftw_real_plan_init(&plan, 0)) {
        return fftw_test_fail("zero-size plan accepted");
    }
    if (!fftw_real_plan_init(&plan, 8)) {
        return fftw_test_fail("plan init");
    }
    if (plan.n_fft != 8) {
        return fftw_test_fail("plan n_fft");
    }
    if (plan.complex_count != 5) {
        return fftw_test_fail("plan complex count");
    }

    impulse[0] = 1.0f;
    for (int32 i = 1; i < 8; i += 1) {
        impulse[i] = 0.0f;
    }

    if (!fftw_real_forward(&plan, impulse, real, imag)) {
        fftw_real_plan_destroy(&plan);
        return fftw_test_fail("forward impulse");
    }
    for (int32 i = 0; i < 5; i += 1) {
        if (!fftw_float_close(real[i], 1.0f)) {
            fftw_real_plan_destroy(&plan);
            return fftw_test_fail("impulse real bin");
        }
        if (!fftw_float_close(imag[i], 0.0f)) {
            fftw_real_plan_destroy(&plan);
            return fftw_test_fail("impulse imaginary bin");
        }
    }

    if (!fftw_real_inverse(&plan, real, imag, output)) {
        fftw_real_plan_destroy(&plan);
        return fftw_test_fail("inverse impulse");
    }
    for (int32 i = 0; i < 8; i += 1) {
        if (!fftw_float_close(output[i], impulse[i])) {
            fftw_real_plan_destroy(&plan);
            return fftw_test_fail("impulse roundtrip");
        }
    }

    for (int32 i = 0; i < 8; i += 1) {
        impulse[i] = (float)(i - 3);
    }
    if (!fftw_real_forward(&plan, impulse, real, imag)) {
        fftw_real_plan_destroy(&plan);
        return fftw_test_fail("forward ramp");
    }
    if (!fftw_real_inverse(&plan, real, imag, output)) {
        fftw_real_plan_destroy(&plan);
        return fftw_test_fail("inverse ramp");
    }
    for (int32 i = 0; i < 8; i += 1) {
        if (!fftw_float_close(output[i], impulse[i])) {
            fftw_real_plan_destroy(&plan);
            return fftw_test_fail("ramp roundtrip");
        }
    }

    fftw_real_plan_destroy(&plan);
    if ((plan.n_fft != 0) || (plan.complex_count != 0)) {
        return fftw_test_fail("destroy resets plan");
    }

    return 0;
}

#endif /* TESTING_fftw */
