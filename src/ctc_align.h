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

static void lrc_ctc_align_result_init(LrcCtcAlignResult *result);
static void lrc_ctc_trellis_init(LrcCtcTrellis *trellis);
static void lrc_ctc_trellis_destroy(LrcCtcTrellis *trellis);
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

#endif /* CTC_ALIGN_H */
