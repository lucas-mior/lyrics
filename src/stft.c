#include "stft.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define STFT_PI 3.14159265358979323846

void
stft_plan_init_empty(StftPlan *plan) {
    plan->n_fft = 0;
    plan->hop = 0;
    plan->complex_count = 0;

    fftw_real_plan_init_empty(&plan->fftw_plan);

    plan->window = 0;
    plan->frame = 0;
    plan->real = 0;
    plan->imag = 0;
    plan->inverse = 0;

    return;
}

bool
stft_plan_init(StftPlan *plan, int32 n_fft, int32 hop) {
    stft_plan_init_empty(plan);

    if (n_fft <= 0 || hop <= 0 || hop > n_fft) {
        return false;
    }

    plan->window = malloc((size_t)n_fft*sizeof(*plan->window));
    plan->frame = malloc((size_t)n_fft*sizeof(*plan->frame));
    plan->inverse = malloc((size_t)n_fft*sizeof(*plan->inverse));
    plan->real = malloc((size_t)(n_fft/2 + 1)*sizeof(*plan->real));
    plan->imag = malloc((size_t)(n_fft/2 + 1)*sizeof(*plan->imag));
    if (plan->window == 0 || plan->frame == 0 || plan->inverse == 0
        || plan->real == 0 || plan->imag == 0) {
        stft_plan_destroy(plan);
        return false;
    }

    if (!fftw_real_plan_init(&plan->fftw_plan, n_fft)) {
        stft_plan_destroy(plan);
        return false;
    }

    plan->n_fft = n_fft;
    plan->hop = hop;
    plan->complex_count = n_fft/2 + 1;
    for (int32 i = 0; i < n_fft; i += 1) {
        double phase;

        phase = 2.0*STFT_PI*(double)i/(double)n_fft;
        plan->window[i] = (float)(0.5 - 0.5*cos(phase));
    }

    return true;
}

int32
stft_frame_count(StftPlan *plan, int64 input_len) {
    int64 count;

    if (plan == 0 || plan->hop <= 0 || input_len < 0) {
        return -1;
    }

    count = input_len/(int64)plan->hop + 1;
    if (count > INT32_MAX) {
        return -1;
    }

    return (int32)count;
}

bool
stft_forward_channel(
    StftPlan *plan,
    float *input,
    int64 input_len,
    float *output_real,
    float *output_imag,
    int32 frame_count
) {
    int32 center;

    if (plan == 0 || input == 0 || output_real == 0 || output_imag == 0) {
        return false;
    }
    if (plan->n_fft <= 0 || plan->hop <= 0 || plan->complex_count <= 0) {
        return false;
    }
    if (input_len < 0 || frame_count != stft_frame_count(plan, input_len)) {
        return false;
    }

    center = plan->n_fft/2;
    for (int32 frame_index = 0; frame_index < frame_count; frame_index += 1) {
        int64 start;

        start = (int64)frame_index*(int64)plan->hop - (int64)center;
        for (int32 i = 0; i < plan->n_fft; i += 1) {
            int64 input_index;

            input_index = start + (int64)i;
            if (input_index < 0 || input_index >= input_len) {
                plan->frame[i] = 0.0f;
            } else {
                plan->frame[i] = input[input_index]*plan->window[i];
            }
        }

        if (!fftw_real_forward(&plan->fftw_plan,
                               plan->frame,
                               plan->real,
                               plan->imag)) {
            return false;
        }

        for (int32 bin = 0; bin < plan->complex_count; bin += 1) {
            output_real[bin*frame_count + frame_index] = plan->real[bin];
            output_imag[bin*frame_count + frame_index] = plan->imag[bin];
        }
    }

    return true;
}

bool
stft_inverse_channel(
    StftPlan *plan,
    float *input_real,
    float *input_imag,
    int32 frame_count,
    float *output,
    int64 output_len
) {
    float *norm;
    int32 center;

    if (plan == 0 || input_real == 0 || input_imag == 0 || output == 0) {
        return false;
    }
    if (plan->n_fft <= 0 || plan->hop <= 0 || plan->complex_count <= 0) {
        return false;
    }
    if (output_len < 0 || frame_count != stft_frame_count(plan, output_len)) {
        return false;
    }

    norm = malloc((size_t)output_len*sizeof(*norm));
    if (norm == 0 && output_len > 0) {
        return false;
    }

    for (int64 i = 0; i < output_len; i += 1) {
        output[i] = 0.0f;
        norm[i] = 0.0f;
    }

    center = plan->n_fft/2;
    for (int32 frame_index = 0; frame_index < frame_count; frame_index += 1) {
        for (int32 bin = 0; bin < plan->complex_count; bin += 1) {
            plan->real[bin] = input_real[bin*frame_count + frame_index];
            plan->imag[bin] = input_imag[bin*frame_count + frame_index];
        }

        if (!fftw_real_inverse(&plan->fftw_plan,
                               plan->real,
                               plan->imag,
                               plan->inverse)) {
            free(norm);
            return false;
        }

        for (int32 i = 0; i < plan->n_fft; i += 1) {
            int64 output_index;
            float window;

            output_index = (int64)frame_index*(int64)plan->hop
                           + (int64)i - (int64)center;
            if (output_index < 0 || output_index >= output_len) {
                continue;
            }

            window = plan->window[i];
            output[output_index] += plan->inverse[i]*window;
            norm[output_index] += window*window;
        }
    }

    for (int64 i = 0; i < output_len; i += 1) {
        if (norm[i] > 0.0000001f) {
            output[i] = output[i]/norm[i];
        }
    }

    free(norm);

    return true;
}

void
stft_plan_destroy(StftPlan *plan) {
    fftw_real_plan_destroy(&plan->fftw_plan);
    free(plan->window);
    free(plan->frame);
    free(plan->real);
    free(plan->imag);
    free(plan->inverse);
    stft_plan_init_empty(plan);

    return;
}

#if TESTING_stft

static int32
stft_test_fail(char *name) {
    fprintf(stderr, "stft test failed: %s\n", name);

    return 1;
}

static bool
stft_float_close(float a, float b) {
    return fabsf(a - b) < 0.001f;
}

int
main(void) {
    StftPlan plan;
    float input[16];
    float output[16];
    float zero_input[8];
    float zero_output[8];
    float zero_real[25];
    float zero_imag[25];
    float real[25];
    float imag[25];
    int32 frame_count;

    stft_plan_init_empty(&plan);
    if (plan.n_fft != 0 || plan.hop != 0 || plan.complex_count != 0) {
        return stft_test_fail("empty dimensions");
    }
    if (stft_plan_init(&plan, 0, 4)) {
        return stft_test_fail("zero n_fft accepted");
    }
    if (stft_plan_init(&plan, 8, 0)) {
        return stft_test_fail("zero hop accepted");
    }
    if (stft_plan_init(&plan, 8, 9)) {
        return stft_test_fail("oversized hop accepted");
    }
    if (!stft_plan_init(&plan, 8, 4)) {
        return stft_test_fail("plan init");
    }
    if (plan.complex_count != 5) {
        stft_plan_destroy(&plan);
        return stft_test_fail("complex count");
    }
    if (!stft_float_close(plan.window[0], 0.0f)) {
        stft_plan_destroy(&plan);
        return stft_test_fail("hann start");
    }
    if (!stft_float_close(plan.window[4], 1.0f)) {
        stft_plan_destroy(&plan);
        return stft_test_fail("hann center");
    }
    if (stft_frame_count(&plan, 16) != 5) {
        stft_plan_destroy(&plan);
        return stft_test_fail("frame count");
    }

    for (int32 i = 0; i < 8; i += 1) {
        zero_input[i] = 0.0f;
    }
    frame_count = stft_frame_count(&plan, 8);
    if (!stft_forward_channel(&plan,
                              zero_input,
                              8,
                              zero_real,
                              zero_imag,
                              frame_count)) {
        stft_plan_destroy(&plan);
        return stft_test_fail("zero forward");
    }
    for (int32 i = 0; i < plan.complex_count*frame_count; i += 1) {
        if (!stft_float_close(zero_real[i], 0.0f)) {
            stft_plan_destroy(&plan);
            return stft_test_fail("zero real bin");
        }
        if (!stft_float_close(zero_imag[i], 0.0f)) {
            stft_plan_destroy(&plan);
            return stft_test_fail("zero imaginary bin");
        }
    }
    if (!stft_inverse_channel(&plan,
                              zero_real,
                              zero_imag,
                              frame_count,
                              zero_output,
                              8)) {
        stft_plan_destroy(&plan);
        return stft_test_fail("zero inverse");
    }
    for (int32 i = 0; i < 8; i += 1) {
        if (!stft_float_close(zero_output[i], 0.0f)) {
            stft_plan_destroy(&plan);
            return stft_test_fail("zero roundtrip");
        }
    }

    for (int32 i = 0; i < 16; i += 1) {
        input[i] = (float)i/16.0f - 0.5f;
    }
    frame_count = stft_frame_count(&plan, 16);
    if (!stft_forward_channel(&plan, input, 16, real, imag, frame_count)) {
        stft_plan_destroy(&plan);
        return stft_test_fail("forward ramp");
    }
    if (!stft_inverse_channel(&plan, real, imag, frame_count, output, 16)) {
        stft_plan_destroy(&plan);
        return stft_test_fail("inverse ramp");
    }
    for (int32 i = 0; i < 16; i += 1) {
        if (!stft_float_close(output[i], input[i])) {
            stft_plan_destroy(&plan);
            return stft_test_fail("ramp roundtrip");
        }
    }

    stft_plan_destroy(&plan);
    if (plan.window != 0 || plan.frame != 0 || plan.real != 0
        || plan.imag != 0 || plan.inverse != 0) {
        return stft_test_fail("destroy reset");
    }

    return 0;
}

#endif /* TESTING_stft */
