#if !defined(CTC_ALIGN_H)
#define CTC_ALIGN_H

#include "cbase.h"
#include "ctc_inference.h"
#include "ctc_tokenizer.h"


enum LrcCtcAlignError {
    LRC_CTC_ALIGN_ERROR_NONE,
    LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT,
    LRC_CTC_ALIGN_ERROR_INVALID_EMISSIONS,
    LRC_CTC_ALIGN_ERROR_INVALID_DIMENSIONS,
    LRC_CTC_ALIGN_ERROR_INVALID_BLANK_TOKEN,
    LRC_CTC_ALIGN_ERROR_INVALID_TARGET_TOKEN,
    LRC_CTC_ALIGN_ERROR_INVALID_TRELLIS,
    LRC_CTC_ALIGN_ERROR_INVALID_PATH,
    LRC_CTC_ALIGN_ERROR_INVALID_TOKEN_SPANS,
    LRC_CTC_ALIGN_ERROR_INVALID_WORD_SPANS,
    LRC_CTC_ALIGN_ERROR_INVALID_TOKENIZED_TEXT,
    LRC_CTC_ALIGN_ERROR_INVALID_NORMALIZED_TEXT,
    LRC_CTC_ALIGN_ERROR_INVALID_FRAME_DURATION,
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
    int64 *previous_states;

    int64 frame_count;
    int64 target_token_count;
    int64 state_count;
    int64 cell_count;

    int32 star_token_id;
    bool has_edge_stars;
    bool has_segment_stars;
} LrcCtcTrellis;

typedef struct LrcCtcPathStep {
    int64 frame_index;
    int64 state_index;
    int64 token_index;
    int32 token_id;

    bool is_blank;
    bool is_star;
} LrcCtcPathStep;

typedef struct LrcCtcPath {
    LrcCtcPathStep *steps;

    int64 step_count;
    int64 step_cap;
} LrcCtcPath;

typedef struct LrcCtcTokenSpan {
    int64 token_index;
    int64 start_frame;
    int64 end_frame;

    float start_seconds;
    float end_seconds;
    float score;

    int32 token_id;
} LrcCtcTokenSpan;

typedef struct LrcCtcTokenSpans {
    LrcCtcTokenSpan *spans;

    int64 span_count;
    int64 span_cap;
} LrcCtcTokenSpans;

typedef struct LrcCtcWordSpan {
    int64 word_index;
    int64 token_start_index;
    int64 token_end_index;
    int64 span_start_index;
    int64 span_end_index;

    int32 normalized_start;
    int32 normalized_end;
    int32 line_index;

    float start_seconds;
    float end_seconds;
    float score;
} LrcCtcWordSpan;

typedef struct LrcCtcWordSpans {
    LrcCtcWordSpan *spans;

    int64 span_count;
    int64 span_cap;
} LrcCtcWordSpans;

enum LrcCtcLineTimestampKind {
    LRC_CTC_LINE_TIMESTAMP_KIND_TIMESTAMPED,
    LRC_CTC_LINE_TIMESTAMP_KIND_BLANK,
};

typedef struct LrcCtcLineTimestamp {
    int64 word_start_index;
    int64 word_end_index;

    int32 line_index;

    float start_seconds;
    float end_seconds;
    float score;

    enum LrcCtcLineTimestampKind kind;
} LrcCtcLineTimestamp;

typedef struct LrcCtcLineTimestamps {
    LrcCtcLineTimestamp *lines;

    int64 line_count;
    int64 line_cap;
    int64 timestamped_line_count;
    int64 blank_line_count;
} LrcCtcLineTimestamps;

static void lrc_ctc_align_result_init(LrcCtcAlignResult *result);
static void lrc_ctc_trellis_init(LrcCtcTrellis *trellis);
static void lrc_ctc_trellis_destroy(LrcCtcTrellis *trellis);
static void lrc_ctc_path_init(LrcCtcPath *path);
static void lrc_ctc_path_destroy(LrcCtcPath *path);
static void lrc_ctc_token_spans_init(LrcCtcTokenSpans *spans);
static void lrc_ctc_token_spans_destroy(LrcCtcTokenSpans *spans);
static void lrc_ctc_word_spans_init(LrcCtcWordSpans *spans);
static void lrc_ctc_word_spans_destroy(LrcCtcWordSpans *spans);
static void lrc_ctc_line_timestamps_init(LrcCtcLineTimestamps *timestamps);
static void lrc_ctc_line_timestamps_destroy(LrcCtcLineTimestamps *timestamps);
static float *lrc_ctc_trellis_cell(
    LrcCtcTrellis *trellis,
    int64 frame_index,
    int64 state_index
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
static bool lrc_ctc_trellis_score_forward_with_edge_stars(
    LrcCtcTrellis *trellis,
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    int64 target_token_count,
    int32 blank_token_id,
    int32 star_token_id,
    LrcCtcAlignResult *result
);
static bool lrc_ctc_trellis_score_forward_with_segment_stars(
    LrcCtcTrellis *trellis,
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    bool *target_segment_starts,
    int64 target_token_count,
    int32 blank_token_id,
    int32 star_token_id,
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
static bool lrc_ctc_trellis_backtrack_with_edge_stars(
    LrcCtcTrellis *trellis,
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    int64 target_token_count,
    int32 blank_token_id,
    int32 star_token_id,
    LrcCtcPath *path,
    LrcCtcAlignResult *result
);
static bool lrc_ctc_trellis_backtrack_with_segment_stars(
    LrcCtcTrellis *trellis,
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    bool *target_segment_starts,
    int64 target_token_count,
    int32 blank_token_id,
    int32 star_token_id,
    LrcCtcPath *path,
    LrcCtcAlignResult *result
);
static bool lrc_ctc_path_to_token_spans(
    LrcCtcPath *path,
    LrcCtcEmissions *emissions,
    float frame_duration_seconds,
    LrcCtcTokenSpans *spans,
    LrcCtcAlignResult *result
);
static bool lrc_ctc_token_spans_to_word_spans(
    LrcCtcTokenSpans *token_spans,
    LrcCtcTokenizedText *tokens,
    LrcLyricsNormalized *normalized,
    LrcCtcWordSpans *word_spans,
    LrcCtcAlignResult *result
);
static bool lrc_ctc_word_spans_to_line_timestamps(
    LrcCtcWordSpans *word_spans,
    LrcLyricsNormalized *normalized,
    LrcCtcLineTimestamps *line_timestamps,
    LrcCtcAlignResult *result
);

#endif /* CTC_ALIGN_H */
