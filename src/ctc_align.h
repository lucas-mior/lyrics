#if !defined(CTC_ALIGN_H)
#define CTC_ALIGN_H

#include "cbase.h"
#include "ctc_inference.h"


enum LrcCtcAlignError {
    LRC_CTC_ALIGN_ERROR_NONE,
    LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT,
    LRC_CTC_ALIGN_ERROR_INVALID_EMISSIONS,
    LRC_CTC_ALIGN_ERROR_INVALID_DIMENSIONS,
    LRC_CTC_ALIGN_ERROR_INVALID_BLANK_TOKEN,
    LRC_CTC_ALIGN_ERROR_INVALID_TARGET_TOKEN,
    LRC_CTC_ALIGN_ERROR_INVALID_TRELLIS,
    LRC_CTC_ALIGN_ERROR_IMPOSSIBLE_ALIGNMENT,
    LRC_CTC_ALIGN_ERROR_TOO_LARGE,
};

typedef struct LrcCtcAlignResult {
    enum LrcCtcAlignError error;
    char *message;

    int64 frame_index;
    int64 token_index;
} LrcCtcAlignResult;

typedef struct LrcCtcTrellis {
    float *scores;

    int64 frame_count;
    int64 target_token_count;
    int64 column_count;
    int64 cell_count;
} LrcCtcTrellis;

typedef struct LrcCtcPathStep {
    int64 frame_index;
    int64 column_index;
    int64 token_index;
    int32 token_id;

    bool is_blank;
} LrcCtcPathStep;

typedef struct LrcCtcPath {
    LrcCtcPathStep *steps;

    int64 step_count;
    int64 step_cap;
} LrcCtcPath;

static void lrc_ctc_align_result_init(LrcCtcAlignResult *result);
static void lrc_ctc_trellis_init(LrcCtcTrellis *trellis);
static void lrc_ctc_trellis_destroy(LrcCtcTrellis *trellis);
static void lrc_ctc_path_init(LrcCtcPath *path);
static void lrc_ctc_path_destroy(LrcCtcPath *path);
static float *lrc_ctc_trellis_cell(
    LrcCtcTrellis *trellis,
    int64 frame_index,
    int64 column_index
);
static bool lrc_ctc_trellis_allocate(
    LrcCtcTrellis *trellis,
    int64 frame_count,
    int64 target_token_count,
    LrcCtcAlignResult *result
);
static bool lrc_ctc_trellis_prepare(
    LrcCtcTrellis *trellis,
    LrcCtcEmissions *emissions,
    int64 target_token_count,
    int32 blank_token_id,
    LrcCtcAlignResult *result
);
static bool lrc_ctc_trellis_score_forward(
    LrcCtcTrellis *trellis,
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    int64 target_token_count,
    int32 blank_token_id,
    LrcCtcAlignResult *result
);
static bool lrc_ctc_trellis_backtrack(
    LrcCtcTrellis *trellis,
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    int64 target_token_count,
    int32 blank_token_id,
    LrcCtcPath *path,
    LrcCtcAlignResult *result
);

#endif /* CTC_ALIGN_H */
