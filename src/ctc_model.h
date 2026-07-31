#if !defined(CTC_MODEL_H)
#define CTC_MODEL_H

#include "cbase.h"
#include "ctc_audio.h"
#define LRC_CTC_MODEL_DEFAULT_INPUTS_TO_LOGITS_RATIO 320
#define LRC_CTC_MODEL_DEFAULT_WINDOW_SECONDS 30
#define LRC_CTC_MODEL_DEFAULT_CONTEXT_SECONDS 2
#define LRC_CTC_MODEL_INPUT_RANK 2

enum LrcCtcModelInputError {
    LRC_CTC_MODEL_INPUT_ERROR_NONE,
    LRC_CTC_MODEL_INPUT_ERROR_INVALID_ARGUMENT,
    LRC_CTC_MODEL_INPUT_ERROR_INVALID_SAMPLE_RATE,
    LRC_CTC_MODEL_INPUT_ERROR_INVALID_RATIO,
    LRC_CTC_MODEL_INPUT_ERROR_INVALID_WINDOW,
    LRC_CTC_MODEL_INPUT_ERROR_INVALID_CONTEXT,
    LRC_CTC_MODEL_INPUT_ERROR_AUDIO_SAMPLE_RATE,
    LRC_CTC_MODEL_INPUT_ERROR_EMPTY_AUDIO,
    LRC_CTC_MODEL_INPUT_ERROR_NON_FINITE_SAMPLE,
    LRC_CTC_MODEL_INPUT_ERROR_TOO_MANY_SAMPLES,
    LRC_CTC_MODEL_INPUT_ERROR_INVALID_MODEL_IO,
};

typedef struct LrcCtcModelConfig {
    int32 sample_rate;
    int32 inputs_to_logits_ratio;
    int32 window_seconds;
    int32 context_seconds;
} LrcCtcModelConfig;

typedef struct LrcCtcModelInputResult {
    enum LrcCtcModelInputError error;
    char *message;

    int64 sample_index;
} LrcCtcModelInputResult;

typedef struct LrcCtcModelIoInfo {
    int64 shape[LRC_CTC_MODEL_INPUT_RANK];

    int32 shape_len;
    int32 count;
    bool is_float32;
} LrcCtcModelIoInfo;

typedef struct LrcCtcModelInput {
    float *samples;

    int64 sample_count;
    int64 original_sample_count;
    int64 extension_sample_count;
    int64 window_sample_count;
    int64 context_sample_count;
    int64 row_count;
    int64 row_sample_count;
    int64 shape[LRC_CTC_MODEL_INPUT_RANK];
    int64 window_frame_count;
    int64 context_frame_count;

    int32 shape_len;
    int32 sample_rate;
    int32 inputs_to_logits_ratio;

    double stride_ms;
    bool chunked;
} LrcCtcModelInput;

static void lrc_ctc_model_config_init(LrcCtcModelConfig *config);
static void lrc_ctc_model_input_result_init(LrcCtcModelInputResult *result);
static void lrc_ctc_model_input_init(LrcCtcModelInput *input);
static void lrc_ctc_model_input_destroy(LrcCtcModelInput *input);
static bool lrc_ctc_model_input_prepare(
    LrcCtcModelInput *input,
    LrcCtcAudio *audio,
    LrcCtcModelConfig *config,
    LrcCtcModelInputResult *result
);
static bool lrc_ctc_model_input_validate_model_io(
    LrcCtcModelInput *input,
    LrcCtcModelIoInfo *info,
    LrcCtcModelInputResult *result
);

#endif /* CTC_MODEL_H */
