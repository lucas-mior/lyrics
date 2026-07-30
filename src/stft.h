#if !defined(STFT_H)
#define STFT_H

#include "../cbase/primitives.h"
#include "fftw.h"

#include <stdbool.h>

typedef struct StftPlan {
    int32 n_fft;
    int32 hop;
    int32 complex_count;

    FftwRealPlan fftw_plan;

    float *window;
    float *frame;
    float *real;
    float *imag;
    float *inverse;
} StftPlan;

void stft_plan_init_empty(StftPlan *plan);
bool stft_plan_init(StftPlan *plan, int32 n_fft, int32 hop);
int32 stft_frame_count(StftPlan *plan, int64 input_len);
bool stft_forward_channel(
    StftPlan *plan,
    float *input,
    int64 input_len,
    float *output_real,
    float *output_imag,
    int32 frame_count
);
bool stft_inverse_channel(
    StftPlan *plan,
    float *input_real,
    float *input_imag,
    int32 frame_count,
    float *output,
    int64 output_len
);
void stft_plan_destroy(StftPlan *plan);

#endif /* STFT_H */
