#if !defined(FFTW_H)
#define FFTW_H

#include "../cbase/primitives.h"

#include <stdbool.h>

typedef struct FftwRealPlan {
    int32 n_fft;
    int32 complex_count;

    float *real;
    void *complex;
    void *forward_plan;
    void *inverse_plan;
} FftwRealPlan;

void fftw_real_plan_init_empty(FftwRealPlan *plan);
bool fftw_real_plan_init(FftwRealPlan *plan, int32 n_fft);
bool fftw_real_forward(
    FftwRealPlan *plan,
    float *input,
    float *output_real,
    float *output_imag
);
bool fftw_real_inverse(
    FftwRealPlan *plan,
    float *input_real,
    float *input_imag,
    float *output
);
void fftw_real_plan_destroy(FftwRealPlan *plan);

#endif /* FFTW_H */
