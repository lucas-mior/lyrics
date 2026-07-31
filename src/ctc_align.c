#include "ctc_align.h"

#include "cbase.h"

#if !defined(TESTING_ctc_align)
#define TESTING_ctc_align 0
#endif

static void
lrc_ctc_align_result_init(LrcCtcAlignResult *result) {
    if (result == NULL) {
        return;
    }

    result->error = LRC_CTC_ALIGN_ERROR_NONE;
    result->message = "ok";

    result->frame_index = -1;
    result->token_index = -1;

    return;
}

static void
lrc_ctc_align_result_set(
    LrcCtcAlignResult *result,
    enum LrcCtcAlignError error,
    char *message,
    int64 frame_index,
    int64 token_index
) {
    if (result == NULL) {
        return;
    }

    result->error = error;
    result->message = message;

    result->frame_index = frame_index;
    result->token_index = token_index;

    return;
}

static void
lrc_ctc_trellis_init(LrcCtcTrellis *trellis) {
    if (trellis == NULL) {
        return;
    }

    memset64(trellis, 0, SIZEOF(*trellis));

    return;
}

static void
lrc_ctc_trellis_destroy(LrcCtcTrellis *trellis) {
    if (trellis == NULL) {
        return;
    }

    if (trellis->scores) {
        free2(trellis->scores,
              trellis->cell_count*SIZEOF(*trellis->scores));
    }

    lrc_ctc_trellis_init(trellis);

    return;
}


static void
lrc_ctc_path_init(LrcCtcPath *path) {
    if (path == NULL) {
        return;
    }

    memset64(path, 0, SIZEOF(*path));

    return;
}

static void
lrc_ctc_path_destroy(LrcCtcPath *path) {
    if (path == NULL) {
        return;
    }

    if (path->steps) {
        free2(path->steps, path->step_cap*SIZEOF(*path->steps));
    }

    lrc_ctc_path_init(path);

    return;
}

static void
lrc_ctc_token_spans_init(LrcCtcTokenSpans *spans) {
    if (spans == NULL) {
        return;
    }

    memset64(spans, 0, SIZEOF(*spans));

    return;
}

static void
lrc_ctc_token_spans_destroy(LrcCtcTokenSpans *spans) {
    if (spans == NULL) {
        return;
    }

    if (spans->spans) {
        free2(spans->spans, spans->span_cap*SIZEOF(*spans->spans));
    }

    lrc_ctc_token_spans_init(spans);

    return;
}

static void
lrc_ctc_word_spans_init(LrcCtcWordSpans *spans) {
    if (spans == NULL) {
        return;
    }

    memset64(spans, 0, SIZEOF(*spans));

    return;
}

static void
lrc_ctc_word_spans_destroy(LrcCtcWordSpans *spans) {
    if (spans == NULL) {
        return;
    }

    if (spans->spans) {
        free2(spans->spans, spans->span_cap*SIZEOF(*spans->spans));
    }

    lrc_ctc_word_spans_init(spans);

    return;
}

static bool
lrc_ctc_token_spans_allocate(
    LrcCtcTokenSpans *spans,
    int64 span_count,
    LrcCtcAlignResult *result
) {
    if (spans == NULL) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT,
            "CTC token spans destination is missing",
            -1,
            -1
        );
        return false;
    }
    if (span_count <= 0) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_PATH,
            "CTC path does not contain token frames",
            -1,
            -1
        );
        return false;
    }
    if (span_count > INT64_MAX/SIZEOF(*spans->spans)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_TOO_LARGE,
            "CTC token span allocation is too large",
            -1,
            span_count
        );
        return false;
    }

    lrc_ctc_token_spans_destroy(spans);
    spans->spans = malloc2(span_count*SIZEOF(*spans->spans));
    spans->span_count = span_count;
    spans->span_cap = span_count;

    for (int64 i = 0; i < spans->span_count; i += 1) {
        spans->spans[i].token_index = -1;
        spans->spans[i].start_frame = -1;
        spans->spans[i].end_frame = -1;
        spans->spans[i].start_seconds = 0.0f;
        spans->spans[i].end_seconds = 0.0f;
        spans->spans[i].score = -INFINITY;
        spans->spans[i].token_id = -1;
    }

    return true;
}

static bool
lrc_ctc_path_allocate(
    LrcCtcPath *path,
    int64 step_count,
    LrcCtcAlignResult *result
) {
    if (path == NULL) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT,
            "CTC path destination is missing",
            -1,
            -1
        );
        return false;
    }
    if (step_count <= 0) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_DIMENSIONS,
            "CTC path step count must be positive",
            step_count,
            -1
        );
        return false;
    }
    if (step_count > INT64_MAX/SIZEOF(*path->steps)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_TOO_LARGE,
            "CTC path allocation is too large",
            step_count,
            -1
        );
        return false;
    }

    lrc_ctc_path_destroy(path);
    path->steps = malloc2(step_count*SIZEOF(*path->steps));
    path->step_count = step_count;
    path->step_cap = step_count;

    for (int64 i = 0; i < path->step_count; i += 1) {
        path->steps[i].frame_index = -1;
        path->steps[i].column_index = -1;
        path->steps[i].token_index = -1;
        path->steps[i].token_id = -1;
        path->steps[i].is_blank = true;
    }

    return true;
}

static bool
lrc_ctc_trellis_dimensions_valid(
    int64 frame_count,
    int64 target_token_count,
    int64 *column_count,
    int64 *cell_count,
    LrcCtcAlignResult *result
) {
    int64 columns;
    int64 cells;

    if ((frame_count <= 0) || (target_token_count <= 0)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_DIMENSIONS,
            "CTC trellis dimensions must be positive",
            frame_count,
            target_token_count
        );
        return false;
    }
    if (target_token_count >= INT64_MAX) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_TOO_LARGE,
            "CTC trellis column count is too large",
            -1,
            target_token_count
        );
        return false;
    }

    columns = target_token_count + 1;
    if (frame_count > INT64_MAX/columns) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_TOO_LARGE,
            "CTC trellis cell count is too large",
            frame_count,
            columns
        );
        return false;
    }

    cells = frame_count*columns;

    *column_count = columns;
    *cell_count = cells;

    return true;
}

static float *
lrc_ctc_trellis_cell(
    LrcCtcTrellis *trellis,
    int64 frame_index,
    int64 column_index
) {
    if (trellis == NULL) {
        return NULL;
    }
    if (trellis->scores == NULL) {
        return NULL;
    }
    if ((frame_index < 0) || (frame_index >= trellis->frame_count)) {
        return NULL;
    }
    if ((column_index < 0) || (column_index >= trellis->column_count)) {
        return NULL;
    }

    return trellis->scores + frame_index*trellis->column_count + column_index;
}

static bool
lrc_ctc_trellis_allocate(
    LrcCtcTrellis *trellis,
    int64 frame_count,
    int64 target_token_count,
    LrcCtcAlignResult *result
) {
    int64 column_count;
    int64 cell_count;

    if (result) {
        lrc_ctc_align_result_init(result);
    }
    if (trellis == NULL) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT,
            "CTC trellis destination is missing",
            -1,
            -1
        );
        return false;
    }

    lrc_ctc_trellis_destroy(trellis);
    if (!lrc_ctc_trellis_dimensions_valid(frame_count,
                                          target_token_count,
                                          &column_count,
                                          &cell_count,
                                          result)) {
        return false;
    }
    if (cell_count > INT64_MAX/SIZEOF(*trellis->scores)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_TOO_LARGE,
            "CTC trellis allocation is too large",
            frame_count,
            column_count
        );
        return false;
    }

    trellis->scores = malloc2(cell_count*SIZEOF(*trellis->scores));
    trellis->frame_count = frame_count;
    trellis->target_token_count = target_token_count;
    trellis->column_count = column_count;
    trellis->cell_count = cell_count;

    for (int64 i = 0; i < trellis->cell_count; i += 1) {
        trellis->scores[i] = -INFINITY;
    }

    return true;
}

static bool
lrc_ctc_emissions_ready(
    LrcCtcEmissions *emissions,
    LrcCtcAlignResult *result
) {
    if (emissions == NULL) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT,
            "CTC emissions are missing",
            -1,
            -1
        );
        return false;
    }
    if ((emissions->values == NULL) || (emissions->frame_count <= 0)
        || (emissions->vocabulary_size <= 0)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_EMISSIONS,
            "CTC emissions are not prepared",
            -1,
            -1
        );
        return false;
    }
    if (emissions->frame_count > INT64_MAX/emissions->vocabulary_size) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_TOO_LARGE,
            "CTC emissions dimensions are too large",
            emissions->frame_count,
            emissions->vocabulary_size
        );
        return false;
    }
    if (emissions->value_count
        != emissions->frame_count*emissions->vocabulary_size) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_EMISSIONS,
            "CTC emissions value count does not match dimensions",
            -1,
            -1
        );
        return false;
    }

    return true;
}

static bool
lrc_ctc_trellis_emissions_ready(
    LrcCtcEmissions *emissions,
    int32 blank_token_id,
    LrcCtcAlignResult *result
) {
    if (!lrc_ctc_emissions_ready(emissions, result)) {
        return false;
    }
    if ((blank_token_id < 0)
        || ((int64)blank_token_id >= emissions->vocabulary_size)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_BLANK_TOKEN,
            "CTC blank token id is outside the vocabulary",
            -1,
            blank_token_id
        );
        return false;
    }

    return true;
}

static bool
lrc_ctc_trellis_prepare(
    LrcCtcTrellis *trellis,
    LrcCtcEmissions *emissions,
    int64 target_token_count,
    int32 blank_token_id,
    LrcCtcAlignResult *result
) {
    float *cell;

    if (result) {
        lrc_ctc_align_result_init(result);
    }
    if (!lrc_ctc_trellis_emissions_ready(emissions,
                                         blank_token_id,
                                         result)) {
        return false;
    }
    if (!lrc_ctc_trellis_allocate(trellis,
                                  emissions->frame_count,
                                  target_token_count,
                                  result)) {
        return false;
    }

    cell = lrc_ctc_trellis_cell(trellis, 0, 0);
    ASSERT(cell != NULL);
    *cell = emissions->values[blank_token_id];
    for (int64 frame = 1; frame < trellis->frame_count; frame += 1) {
        float previous;
        float blank_score;

        cell = lrc_ctc_trellis_cell(trellis, frame - 1, 0);
        ASSERT(cell != NULL);
        previous = *cell;
        blank_score = emissions->values[frame*emissions->vocabulary_size
                                        + blank_token_id];

        cell = lrc_ctc_trellis_cell(trellis, frame, 0);
        ASSERT(cell != NULL);
        *cell = previous + blank_score;
    }

    return true;
}


static float
lrc_ctc_emission_value(
    LrcCtcEmissions *emissions,
    int64 frame_index,
    int32 token_id
) {
    int64 index;

    ASSERT(emissions != NULL);
    ASSERT(emissions->values != NULL);
    ASSERT(frame_index >= 0);
    ASSERT(frame_index < emissions->frame_count);
    ASSERT(token_id >= 0);
    ASSERT((int64)token_id < emissions->vocabulary_size);

    index = frame_index*emissions->vocabulary_size + token_id;

    return emissions->values[index];
}

static bool
lrc_ctc_target_tokens_valid(
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    int64 target_token_count,
    int32 blank_token_id,
    LrcCtcAlignResult *result
) {
    if (target_token_ids == NULL) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT,
            "CTC target token ids are missing",
            -1,
            -1
        );
        return false;
    }
    if (target_token_count <= 0) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_DIMENSIONS,
            "CTC target token count must be positive",
            -1,
            target_token_count
        );
        return false;
    }

    for (int64 i = 0; i < target_token_count; i += 1) {
        if ((target_token_ids[i] < 0)
            || ((int64)target_token_ids[i] >= emissions->vocabulary_size)
            || (target_token_ids[i] == blank_token_id)) {
            lrc_ctc_align_result_set(
                result,
                LRC_CTC_ALIGN_ERROR_INVALID_TARGET_TOKEN,
                "CTC target token id is invalid",
                -1,
                i
            );
            return false;
        }
    }

    return true;
}

static float
lrc_ctc_best_score(float stay_score, float advance_score) {
    if (advance_score > stay_score) {
        return advance_score;
    }

    return stay_score;
}

static bool
lrc_ctc_trellis_score_forward(
    LrcCtcTrellis *trellis,
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    int64 target_token_count,
    int32 blank_token_id,
    LrcCtcAlignResult *result
) {
    if (result) {
        lrc_ctc_align_result_init(result);
    }
    if (!lrc_ctc_trellis_emissions_ready(emissions,
                                         blank_token_id,
                                         result)) {
        return false;
    }
    if (!lrc_ctc_target_tokens_valid(emissions,
                                     target_token_ids,
                                     target_token_count,
                                     blank_token_id,
                                     result)) {
        return false;
    }
    if (!lrc_ctc_trellis_prepare(trellis,
                                 emissions,
                                 target_token_count,
                                 blank_token_id,
                                 result)) {
        return false;
    }

    for (int64 frame = 1; frame < trellis->frame_count; frame += 1) {
        for (int64 column = 1; column < trellis->column_count; column += 1) {
            float previous_same;
            float previous_advance;
            float blank_score;
            float token_score;
            float best_score;
            float *cell;

            cell = lrc_ctc_trellis_cell(trellis, frame - 1, column);
            ASSERT(cell != NULL);
            previous_same = *cell;

            cell = lrc_ctc_trellis_cell(trellis, frame - 1, column - 1);
            ASSERT(cell != NULL);
            previous_advance = *cell;

            blank_score = previous_same
                          + lrc_ctc_emission_value(emissions,
                                                   frame,
                                                   blank_token_id);
            token_score = previous_advance
                          + lrc_ctc_emission_value(
                              emissions,
                              frame,
                              target_token_ids[column - 1]
                          );
            best_score = lrc_ctc_best_score(blank_score, token_score);

            cell = lrc_ctc_trellis_cell(trellis, frame, column);
            ASSERT(cell != NULL);
            *cell = best_score;
        }
    }

    return true;
}


static bool
lrc_ctc_trellis_ready_for_backtracking(
    LrcCtcTrellis *trellis,
    LrcCtcEmissions *emissions,
    int64 target_token_count,
    LrcCtcAlignResult *result
) {
    if (trellis == NULL) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT,
            "CTC trellis is missing",
            -1,
            -1
        );
        return false;
    }
    if (trellis->scores == NULL) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_TRELLIS,
            "CTC trellis has not been scored",
            -1,
            -1
        );
        return false;
    }
    if ((trellis->frame_count != emissions->frame_count)
        || (trellis->target_token_count != target_token_count)
        || (trellis->column_count != target_token_count + 1)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_TRELLIS,
            "CTC trellis dimensions do not match inputs",
            trellis->frame_count,
            trellis->target_token_count
        );
        return false;
    }
    if (trellis->cell_count
        != trellis->frame_count*trellis->column_count) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_TRELLIS,
            "CTC trellis cell count does not match dimensions",
            trellis->frame_count,
            trellis->column_count
        );
        return false;
    }

    return true;
}

static bool
lrc_ctc_score_close(float a, float b) {
    float diff;

    if (!isfinite(a) || !isfinite(b)) {
        return false;
    }

    diff = fabsf(a - b);

    return diff <= 0.0001f;
}

static bool
lrc_ctc_score_can_backtrack(float current, float previous, float emission) {
    if (!isfinite(current)) {
        return false;
    }
    if (!isfinite(previous)) {
        return false;
    }
    if (!isfinite(emission)) {
        return false;
    }

    return lrc_ctc_score_close(current, previous + emission);
}

static void
lrc_ctc_path_set_blank_step(
    LrcCtcPath *path,
    int64 frame_index,
    int64 column_index,
    int32 blank_token_id
) {
    ASSERT(path != NULL);
    ASSERT(path->steps != NULL);
    ASSERT(frame_index >= 0);
    ASSERT(frame_index < path->step_count);

    path->steps[frame_index].frame_index = frame_index;
    path->steps[frame_index].column_index = column_index;
    path->steps[frame_index].token_index = -1;
    path->steps[frame_index].token_id = blank_token_id;
    path->steps[frame_index].is_blank = true;

    return;
}

static void
lrc_ctc_path_set_token_step(
    LrcCtcPath *path,
    int64 frame_index,
    int64 column_index,
    int32 token_id
) {
    ASSERT(path != NULL);
    ASSERT(path->steps != NULL);
    ASSERT(frame_index >= 0);
    ASSERT(frame_index < path->step_count);
    ASSERT(column_index > 0);

    path->steps[frame_index].frame_index = frame_index;
    path->steps[frame_index].column_index = column_index;
    path->steps[frame_index].token_index = column_index - 1;
    path->steps[frame_index].token_id = token_id;
    path->steps[frame_index].is_blank = false;

    return;
}

static bool
lrc_ctc_trellis_backtrack_step(
    LrcCtcTrellis *trellis,
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    int32 blank_token_id,
    LrcCtcPath *path,
    int64 frame,
    int64 *column,
    LrcCtcAlignResult *result
) {
    float *cell;
    float current_score;
    float previous_same;
    float previous_advance;
    float blank_score;
    float token_score;
    bool can_stay;
    bool can_advance;

    ASSERT(column != NULL);
    ASSERT(*column >= 0);
    ASSERT(*column < trellis->column_count);
    ASSERT(frame > 0);

    if (*column == 0) {
        lrc_ctc_path_set_blank_step(path, frame, *column, blank_token_id);
        return true;
    }

    cell = lrc_ctc_trellis_cell(trellis, frame, *column);
    ASSERT(cell != NULL);
    current_score = *cell;
    if (!isfinite(current_score)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_IMPOSSIBLE_ALIGNMENT,
            "CTC trellis final path is impossible",
            frame,
            *column - 1
        );
        return false;
    }

    cell = lrc_ctc_trellis_cell(trellis, frame - 1, *column);
    ASSERT(cell != NULL);
    previous_same = *cell;

    cell = lrc_ctc_trellis_cell(trellis, frame - 1, *column - 1);
    ASSERT(cell != NULL);
    previous_advance = *cell;

    blank_score = lrc_ctc_emission_value(emissions, frame, blank_token_id);
    token_score = lrc_ctc_emission_value(
        emissions,
        frame,
        target_token_ids[*column - 1]
    );
    can_stay = lrc_ctc_score_can_backtrack(current_score,
                                           previous_same,
                                           blank_score);
    can_advance = lrc_ctc_score_can_backtrack(current_score,
                                              previous_advance,
                                              token_score);

    if (can_advance && (!can_stay
                        || (previous_advance + token_score
                            >= previous_same + blank_score))) {
        lrc_ctc_path_set_token_step(path,
                                    frame,
                                    *column,
                                    target_token_ids[*column - 1]);
        *column -= 1;
        return true;
    }
    if (can_stay) {
        lrc_ctc_path_set_blank_step(path, frame, *column, blank_token_id);
        return true;
    }

    lrc_ctc_align_result_set(
        result,
        LRC_CTC_ALIGN_ERROR_INVALID_TRELLIS,
        "CTC trellis cannot be backtracked from this cell",
        frame,
        *column - 1
    );
    return false;
}

static bool
lrc_ctc_trellis_backtrack(
    LrcCtcTrellis *trellis,
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    int64 target_token_count,
    int32 blank_token_id,
    LrcCtcPath *path,
    LrcCtcAlignResult *result
) {
    float *final_cell;
    int64 column;

    if (result) {
        lrc_ctc_align_result_init(result);
    }
    if (!lrc_ctc_trellis_emissions_ready(emissions,
                                         blank_token_id,
                                         result)) {
        return false;
    }
    if (!lrc_ctc_target_tokens_valid(emissions,
                                     target_token_ids,
                                     target_token_count,
                                     blank_token_id,
                                     result)) {
        return false;
    }
    if (!lrc_ctc_trellis_ready_for_backtracking(trellis,
                                                emissions,
                                                target_token_count,
                                                result)) {
        return false;
    }
    if (!lrc_ctc_path_allocate(path, trellis->frame_count, result)) {
        return false;
    }

    column = trellis->target_token_count;
    final_cell = lrc_ctc_trellis_cell(trellis,
                                      trellis->frame_count - 1,
                                      column);
    ASSERT(final_cell != NULL);
    if (!isfinite(*final_cell)) {
        lrc_ctc_path_destroy(path);
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_IMPOSSIBLE_ALIGNMENT,
            "CTC target tokens cannot fit in the available frames",
            trellis->frame_count - 1,
            trellis->target_token_count - 1
        );
        return false;
    }

    for (int64 frame = trellis->frame_count - 1; frame > 0; frame -= 1) {
        if (!lrc_ctc_trellis_backtrack_step(trellis,
                                            emissions,
                                            target_token_ids,
                                            blank_token_id,
                                            path,
                                            frame,
                                            &column,
                                            result)) {
            lrc_ctc_path_destroy(path);
            return false;
        }
    }
    if (column != 0) {
        lrc_ctc_path_destroy(path);
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_IMPOSSIBLE_ALIGNMENT,
            "CTC backtracking did not consume all target tokens",
            0,
            column - 1
        );
        return false;
    }

    lrc_ctc_path_set_blank_step(path, 0, 0, blank_token_id);

    return true;
}

static bool
lrc_ctc_path_step_valid(
    LrcCtcPathStep *step,
    LrcCtcEmissions *emissions,
    LrcCtcAlignResult *result
) {
    if ((step->frame_index < 0)
        || (step->frame_index >= emissions->frame_count)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_PATH,
            "CTC path frame index is outside emissions",
            step->frame_index,
            step->token_index
        );
        return false;
    }
    if (step->is_blank) {
        return true;
    }
    if (step->token_index < 0) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_PATH,
            "CTC path token index is invalid",
            step->frame_index,
            step->token_index
        );
        return false;
    }
    if ((step->token_id < 0)
        || ((int64)step->token_id >= emissions->vocabulary_size)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_PATH,
            "CTC path token id is outside emissions",
            step->frame_index,
            step->token_index
        );
        return false;
    }

    return true;
}

static bool
lrc_ctc_path_ready_for_spans(
    LrcCtcPath *path,
    LrcCtcEmissions *emissions,
    float frame_duration_seconds,
    LrcCtcAlignResult *result
) {
    if (path == NULL) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT,
            "CTC path is missing",
            -1,
            -1
        );
        return false;
    }
    if ((path->steps == NULL) || (path->step_count <= 0)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_PATH,
            "CTC path has no steps",
            -1,
            -1
        );
        return false;
    }
    if (!isfinite(frame_duration_seconds)
        || (frame_duration_seconds <= 0.0f)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_FRAME_DURATION,
            "CTC frame duration must be positive and finite",
            -1,
            -1
        );
        return false;
    }
    if (!lrc_ctc_emissions_ready(emissions, result)) {
        return false;
    }

    for (int64 i = 0; i < path->step_count; i += 1) {
        if (!lrc_ctc_path_step_valid(path->steps + i, emissions, result)) {
            return false;
        }
    }

    return true;
}

static bool
lrc_ctc_path_step_starts_span(
    LrcCtcPath *path,
    int64 step_index
) {
    LrcCtcPathStep *step;
    LrcCtcPathStep *previous;

    ASSERT(path != NULL);
    ASSERT(path->steps != NULL);
    ASSERT(step_index >= 0);
    ASSERT(step_index < path->step_count);

    step = path->steps + step_index;
    if (step->is_blank) {
        return false;
    }
    if (step_index <= 0) {
        return true;
    }

    previous = path->steps + step_index - 1;
    if (previous->is_blank) {
        return true;
    }

    return previous->token_index != step->token_index;
}

static int64
lrc_ctc_path_count_token_spans(LrcCtcPath *path) {
    int64 count;

    ASSERT(path != NULL);
    ASSERT(path->steps != NULL);

    count = 0;
    for (int64 i = 0; i < path->step_count; i += 1) {
        if (lrc_ctc_path_step_starts_span(path, i)) {
            count += 1;
        }
    }

    return count;
}

static void
lrc_ctc_token_span_finish(
    LrcCtcTokenSpan *span,
    int64 score_count,
    float score_sum,
    float frame_duration_seconds
) {
    ASSERT(span != NULL);
    ASSERT(span->start_frame >= 0);
    ASSERT(span->end_frame > span->start_frame);
    ASSERT(score_count > 0);

    span->start_seconds = (float)span->start_frame*frame_duration_seconds;
    span->end_seconds = (float)span->end_frame*frame_duration_seconds;
    span->score = score_sum/(float)score_count;

    return;
}

static bool
lrc_ctc_path_to_token_spans(
    LrcCtcPath *path,
    LrcCtcEmissions *emissions,
    float frame_duration_seconds,
    LrcCtcTokenSpans *spans,
    LrcCtcAlignResult *result
) {
    int64 span_count;
    int64 span_index;
    int64 score_count;
    float score_sum;
    LrcCtcTokenSpan *span;

    if (result) {
        lrc_ctc_align_result_init(result);
    }
    if (!lrc_ctc_path_ready_for_spans(path,
                                      emissions,
                                      frame_duration_seconds,
                                      result)) {
        return false;
    }

    span_count = lrc_ctc_path_count_token_spans(path);
    if (!lrc_ctc_token_spans_allocate(spans, span_count, result)) {
        return false;
    }

    span_index = -1;
    score_count = 0;
    score_sum = 0.0f;
    span = NULL;
    for (int64 i = 0; i < path->step_count; i += 1) {
        LrcCtcPathStep *step = path->steps + i;
        float score;

        if (step->is_blank) {
            if (span) {
                lrc_ctc_token_span_finish(span,
                                          score_count,
                                          score_sum,
                                          frame_duration_seconds);
                span = NULL;
                score_count = 0;
                score_sum = 0.0f;
            }
            continue;
        }

        if (lrc_ctc_path_step_starts_span(path, i)) {
            if (span) {
                lrc_ctc_token_span_finish(span,
                                          score_count,
                                          score_sum,
                                          frame_duration_seconds);
            }

            span_index += 1;
            ASSERT(span_index < spans->span_count);
            span = spans->spans + span_index;
            span->token_index = step->token_index;
            span->start_frame = step->frame_index;
            span->end_frame = step->frame_index + 1;
            span->token_id = step->token_id;
            score_count = 0;
            score_sum = 0.0f;
        }

        ASSERT(span != NULL);
        if (step->frame_index + 1 > span->end_frame) {
            span->end_frame = step->frame_index + 1;
        }

        score = lrc_ctc_emission_value(emissions,
                                       step->frame_index,
                                       step->token_id);
        score_sum += score;
        score_count += 1;
    }

    if (span) {
        lrc_ctc_token_span_finish(span,
                                  score_count,
                                  score_sum,
                                  frame_duration_seconds);
    }
    ASSERT(span_index + 1 == spans->span_count);

    return true;
}

static bool
lrc_ctc_word_spans_allocate(
    LrcCtcWordSpans *spans,
    int64 span_count,
    LrcCtcAlignResult *result
) {
    if (spans == NULL) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT,
            "CTC word spans destination is missing",
            -1,
            -1
        );
        return false;
    }
    if (span_count <= 0) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_TOKENIZED_TEXT,
            "CTC token spans did not produce words",
            -1,
            -1
        );
        return false;
    }
    if (span_count > INT64_MAX/SIZEOF(*spans->spans)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_TOO_LARGE,
            "CTC word span allocation is too large",
            -1,
            span_count
        );
        return false;
    }

    lrc_ctc_word_spans_destroy(spans);
    spans->spans = malloc2(span_count*SIZEOF(*spans->spans));
    spans->span_count = span_count;
    spans->span_cap = span_count;

    for (int64 i = 0; i < spans->span_count; i += 1) {
        spans->spans[i].word_index = -1;
        spans->spans[i].token_start_index = -1;
        spans->spans[i].token_end_index = -1;
        spans->spans[i].span_start_index = -1;
        spans->spans[i].span_end_index = -1;

        spans->spans[i].normalized_start = -1;
        spans->spans[i].normalized_end = -1;
        spans->spans[i].line_index = -1;

        spans->spans[i].start_seconds = 0.0f;
        spans->spans[i].end_seconds = 0.0f;
        spans->spans[i].score = -INFINITY;
    }

    return true;
}

static bool
lrc_ctc_word_inputs_ready(
    LrcCtcTokenSpans *token_spans,
    LrcCtcTokenizedText *tokens,
    LrcLyricsNormalized *normalized,
    LrcCtcWordSpans *word_spans,
    LrcCtcAlignResult *result
) {
    if ((token_spans == NULL) || (tokens == NULL)
        || (normalized == NULL) || (word_spans == NULL)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT,
            "CTC word-span conversion received invalid arguments",
            -1,
            -1
        );
        return false;
    }
    if ((token_spans->spans == NULL) || (token_spans->span_count <= 0)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_TOKEN_SPANS,
            "CTC token spans are empty",
            -1,
            -1
        );
        return false;
    }
    if ((tokens->tokens == NULL) || (tokens->token_count <= 0)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_TOKENIZED_TEXT,
            "CTC tokenized text is empty",
            -1,
            -1
        );
        return false;
    }
    if ((normalized->text == NULL) || (normalized->text_len <= 0)
        || (normalized->byte_count != normalized->text_len)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_NORMALIZED_TEXT,
            "normalized lyrics are not ready",
            -1,
            -1
        );
        return false;
    }
    if (token_spans->span_count != tokens->token_count) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_TOKEN_SPANS,
            "CTC token spans must match tokenized text",
            token_spans->span_count,
            tokens->token_count
        );
        return false;
    }

    return true;
}

static bool
lrc_ctc_token_range_valid(
    LrcCtcTextToken *token,
    LrcLyricsNormalized *normalized,
    int64 token_index,
    LrcCtcAlignResult *result
) {
    if ((token->normalized_start < 0)
        || (token->normalized_end <= token->normalized_start)
        || (token->normalized_end > normalized->text_len)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_TOKENIZED_TEXT,
            "CTC token normalized range is invalid",
            token->normalized_start,
            token_index
        );
        return false;
    }
    if ((token->line_index < 0)
        || (token->line_index >= normalized->line_count)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_TOKENIZED_TEXT,
            "CTC token line index is invalid",
            -1,
            token_index
        );
        return false;
    }

    return true;
}

static bool
lrc_ctc_token_span_matches_token(
    LrcCtcTokenSpan *span,
    LrcCtcTextToken *token,
    int64 span_index,
    LrcCtcAlignResult *result
) {
    if (span->token_index != span_index) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_TOKEN_SPANS,
            "CTC token span index does not match tokenized text",
            -1,
            span_index
        );
        return false;
    }
    if (span->token_id != token->token_id) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_TOKEN_SPANS,
            "CTC token span id does not match tokenized text",
            -1,
            span_index
        );
        return false;
    }
    if (!isfinite(span->start_seconds) || !isfinite(span->end_seconds)
        || (span->end_seconds < span->start_seconds)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_TOKEN_SPANS,
            "CTC token span timing is invalid",
            -1,
            span_index
        );
        return false;
    }
    if (!isfinite(span->score)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_TOKEN_SPANS,
            "CTC token span score is invalid",
            -1,
            span_index
        );
        return false;
    }

    return true;
}

static bool
lrc_ctc_normalized_range_is_space(
    LrcLyricsNormalized *normalized,
    int32 start,
    int32 end
) {
    ASSERT(normalized != NULL);
    ASSERT(normalized->text != NULL);
    ASSERT(start >= 0);
    ASSERT(end > start);
    ASSERT(end <= normalized->text_len);

    for (int32 i = start; i < end; i += 1) {
        if (normalized->text[i] != ' ') {
            return false;
        }
    }

    return true;
}

static bool
lrc_ctc_normalized_range_has_space(
    LrcLyricsNormalized *normalized,
    int32 start,
    int32 end
) {
    ASSERT(normalized != NULL);
    ASSERT(normalized->text != NULL);
    ASSERT(start >= 0);
    ASSERT(end > start);
    ASSERT(end <= normalized->text_len);

    for (int32 i = start; i < end; i += 1) {
        if (normalized->text[i] == ' ') {
            return true;
        }
    }

    return false;
}

static bool
lrc_ctc_word_count(
    LrcCtcTokenSpans *token_spans,
    LrcCtcTokenizedText *tokens,
    LrcLyricsNormalized *normalized,
    int64 *word_count,
    LrcCtcAlignResult *result
) {
    bool in_word;

    ASSERT(word_count != NULL);

    *word_count = 0;
    in_word = false;
    for (int64 i = 0; i < token_spans->span_count; i += 1) {
        LrcCtcTokenSpan *span;
        LrcCtcTextToken *token;

        span = token_spans->spans + i;
        token = tokens->tokens + i;
        if (!lrc_ctc_token_range_valid(token, normalized, i, result)) {
            return false;
        }
        if (!lrc_ctc_token_span_matches_token(span, token, i, result)) {
            return false;
        }
        if (lrc_ctc_normalized_range_is_space(normalized,
                                              token->normalized_start,
                                              token->normalized_end)) {
            in_word = false;
            continue;
        }
        if (lrc_ctc_normalized_range_has_space(normalized,
                                               token->normalized_start,
                                               token->normalized_end)) {
            lrc_ctc_align_result_set(
                result,
                LRC_CTC_ALIGN_ERROR_INVALID_TOKENIZED_TEXT,
                "CTC token spans cannot split a mixed word/space token",
                token->normalized_start,
                i
            );
            return false;
        }
        if (!in_word) {
            *word_count += 1;
            in_word = true;
        }
    }

    return true;
}

static void
lrc_ctc_word_span_start(
    LrcCtcWordSpan *word,
    int64 word_index,
    int64 span_index,
    LrcCtcTokenSpan *token_span,
    LrcCtcTextToken *token
) {
    word->word_index = word_index;
    word->token_start_index = token_span->token_index;
    word->token_end_index = token_span->token_index + 1;
    word->span_start_index = span_index;
    word->span_end_index = span_index + 1;

    word->normalized_start = token->normalized_start;
    word->normalized_end = token->normalized_end;
    word->line_index = token->line_index;

    word->start_seconds = token_span->start_seconds;
    word->end_seconds = token_span->end_seconds;
    word->score = token_span->score;

    return;
}

static bool
lrc_ctc_word_span_extend(
    LrcCtcWordSpan *word,
    int64 span_index,
    LrcCtcTokenSpan *token_span,
    LrcCtcTextToken *token,
    int64 score_count,
    float *score_sum,
    LrcCtcAlignResult *result
) {
    if (token->line_index != word->line_index) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_TOKENIZED_TEXT,
            "CTC word cannot cross lyric lines",
            token->normalized_start,
            token_span->token_index
        );
        return false;
    }
    if ((token_span->start_seconds + 0.00001f) < word->end_seconds) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_TOKEN_SPANS,
            "CTC token spans must be time ordered",
            -1,
            token_span->token_index
        );
        return false;
    }

    word->token_end_index = token_span->token_index + 1;
    word->span_end_index = span_index + 1;
    word->normalized_end = token->normalized_end;
    word->end_seconds = token_span->end_seconds;
    *score_sum += token_span->score;
    word->score = *score_sum/(float)score_count;

    return true;
}

static bool
lrc_ctc_token_spans_to_word_spans(
    LrcCtcTokenSpans *token_spans,
    LrcCtcTokenizedText *tokens,
    LrcLyricsNormalized *normalized,
    LrcCtcWordSpans *word_spans,
    LrcCtcAlignResult *result
) {
    LrcCtcWordSpan *word;
    int64 word_count;
    int64 word_index;
    int64 score_count;
    float score_sum;

    if (result) {
        lrc_ctc_align_result_init(result);
    }
    if (!lrc_ctc_word_inputs_ready(token_spans,
                                   tokens,
                                   normalized,
                                   word_spans,
                                   result)) {
        return false;
    }

    lrc_ctc_word_spans_destroy(word_spans);
    if (!lrc_ctc_word_count(token_spans,
                            tokens,
                            normalized,
                            &word_count,
                            result)) {
        return false;
    }
    if (!lrc_ctc_word_spans_allocate(word_spans, word_count, result)) {
        return false;
    }

    word = NULL;
    word_index = -1;
    score_count = 0;
    score_sum = 0.0f;
    for (int64 i = 0; i < token_spans->span_count; i += 1) {
        LrcCtcTokenSpan *token_span;
        LrcCtcTextToken *token;

        token_span = token_spans->spans + i;
        token = tokens->tokens + i;
        if (lrc_ctc_normalized_range_is_space(normalized,
                                              token->normalized_start,
                                              token->normalized_end)) {
            word = NULL;
            score_count = 0;
            score_sum = 0.0f;
            continue;
        }

        if (word == NULL) {
            word_index += 1;
            ASSERT(word_index < word_spans->span_count);
            word = word_spans->spans + word_index;
            score_count = 1;
            score_sum = token_span->score;
            lrc_ctc_word_span_start(word,
                                    word_index,
                                    i,
                                    token_span,
                                    token);
            continue;
        }

        score_count += 1;
        if (!lrc_ctc_word_span_extend(word,
                                      i,
                                      token_span,
                                      token,
                                      score_count,
                                      &score_sum,
                                      result)) {
            lrc_ctc_word_spans_destroy(word_spans);
            return false;
        }
    }
    ASSERT(word_index + 1 == word_spans->span_count);

    return true;
}

#if TESTING_ctc_align

#define CBASE_IMPLEMENT
#include "cbase.h"

#include "lyrics.c"
#include "ctc_tokenizer.c"

static int32
ctc_align_test_fail(char *name) {
    error2("CTC align test failed: %s\n", name);

    return 1;
}

static bool
ctc_align_float_close(float a, float b, float max_error) {
    float diff;

    diff = fabsf(a - b);

    return diff <= max_error;
}

static bool
ctc_align_is_negative_infinity(float value) {
    if (!isinf(value)) {
        return false;
    }

    return value < 0.0f;
}

static void
ctc_align_make_emissions(
    LrcCtcEmissions *emissions,
    float *values,
    int64 frame_count,
    int64 vocabulary_size
) {
    memset64(emissions, 0, SIZEOF(*emissions));

    emissions->values = values;
    emissions->value_count = frame_count*vocabulary_size;
    emissions->row_count = 1;
    emissions->row_frame_count = frame_count;
    emissions->frame_count = frame_count;
    emissions->vocabulary_size = vocabulary_size;
    emissions->shape_len = 2;
    emissions->shape[0] = frame_count;
    emissions->shape[1] = vocabulary_size;

    return;
}

static void
ctc_align_join_path(
    char *buffer,
    int64 buffer_len,
    char *dir,
    char *name
) {
    int32 len;

    len = snprintf2(buffer, buffer_len, "%s/%s", dir, name);
    ASSERT(len > 0);
    ASSERT(len < buffer_len);

    return;
}

static bool
ctc_align_load_alphabet_tokenizer(LrcCtcTokenizer *tokenizer) {
    LrcCtcTokenizerResult result;
    StrBuilder builder;
    char temp_dir[PATH_MAX];
    char path[PATH_MAX];
    bool ok;

    sb_init(&builder);
    SB_APPEND(&builder, "<blank>\n<space>\n");
    for (char ch = 'a'; ch <= 'z'; ch += 1) {
        sb_append(&builder, &ch, 1);
        SB_APPEND(&builder, "\n");
    }

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "ctc_align_tokens");
    ctc_align_join_path(path, SIZEOF(path), temp_dir, "tokens.txt");
    if (!write_entire_file(path, builder.data, builder.len)) {
        test_remove_tree(temp_dir);
        sb_free(&builder);
        return false;
    }

    lrc_ctc_tokenizer_init(tokenizer);
    ok = lrc_ctc_tokenizer_load_file(tokenizer, path, &result);
    test_remove_tree(temp_dir);
    sb_free(&builder);

    return ok;
}

static bool
ctc_align_load_lyrics_text(
    LrcLyrics *lyrics,
    char *text,
    int32 text_len
) {
    LrcLyricsLoadResult result;
    char temp_dir[PATH_MAX];
    char path[PATH_MAX];
    bool ok;

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "ctc_align_lyrics");
    ctc_align_join_path(path, SIZEOF(path), temp_dir, "lyrics.txt");
    if (!write_entire_file(path, text, text_len)) {
        test_remove_tree(temp_dir);
        return false;
    }

    lrc_lyrics_init(lyrics);
    ok = lrc_lyrics_load_file(lyrics, path, &result);
    test_remove_tree(temp_dir);

    return ok;
}

static bool
ctc_align_make_token_spans_from_tokens(
    LrcCtcTokenizedText *tokens,
    float first_start_seconds,
    float token_seconds,
    LrcCtcTokenSpans *spans
) {
    LrcCtcAlignResult result;

    if (!lrc_ctc_token_spans_allocate(spans,
                                      tokens->token_count,
                                      &result)) {
        return false;
    }

    for (int32 i = 0; i < tokens->token_count; i += 1) {
        LrcCtcTokenSpan *span;
        float start_seconds;

        start_seconds = first_start_seconds + (float)i*token_seconds;
        span = spans->spans + i;
        span->token_index = i;
        span->start_frame = i;
        span->end_frame = i + 1;
        span->start_seconds = start_seconds;
        span->end_seconds = start_seconds + token_seconds;
        span->score = -0.10f - (float)i*0.01f;
        span->token_id = tokens->tokens[i].token_id;
    }

    return true;
}

static bool
ctc_align_load_tokenized_lyrics(
    char *text,
    int32 text_len,
    LrcLyrics *lyrics,
    LrcLyricsNormalized *normalized,
    LrcCtcTokenizer *tokenizer,
    LrcCtcTokenizedText *tokens
) {
    LrcCtcTokenizeResult result;

    lrc_lyrics_normalized_init(normalized);
    lrc_ctc_tokenizer_init(tokenizer);
    lrc_ctc_tokenized_text_init(tokens);
    if (!ctc_align_load_lyrics_text(lyrics, text, text_len)) {
        return false;
    }
    if (!lrc_lyrics_normalize(lyrics, normalized)) {
        return false;
    }
    if (!ctc_align_load_alphabet_tokenizer(tokenizer)) {
        return false;
    }
    if (!lrc_ctc_tokenizer_tokenize_normalized(tokenizer,
                                               normalized,
                                               tokens,
                                               &result)) {
        return false;
    }

    return true;
}

static void
ctc_align_assert_word_text(
    LrcLyricsNormalized *normalized,
    LrcCtcWordSpan *word,
    char *text,
    int32 text_len
) {
    ASSERT(word->normalized_start >= 0);
    ASSERT(word->normalized_end > word->normalized_start);
    ASSERT(word->normalized_end <= normalized->text_len);
    ASSERT(strequal2(normalized->text + word->normalized_start,
                     word->normalized_end - word->normalized_start,
                     text,
                     text_len));

    return;
}

static void
ctc_align_fill_predictable_values(
    float *values,
    int64 frame_count,
    int64 vocabulary_size,
    int32 blank_token_id,
    int32 *token_ids,
    int64 token_count
) {
    for (int64 i = 0; i < frame_count*vocabulary_size; i += 1) {
        values[i] = -12.0f;
    }

    for (int64 frame = 0; frame < frame_count; frame += 1) {
        values[frame*vocabulary_size + blank_token_id] = -0.05f;
    }
    for (int64 i = 0; i < token_count; i += 1) {
        int64 frame = i + 1;

        values[frame*vocabulary_size + blank_token_id] = -6.0f;
        values[frame*vocabulary_size + token_ids[i]] = -0.05f;
    }

    return;
}

static int32
ctc_align_test_empty_initializers(void) {
    LrcCtcAlignResult result;
    LrcCtcTrellis trellis;
    LrcCtcPath path;
    LrcCtcTokenSpans spans;
    LrcCtcWordSpans word_spans;

    lrc_ctc_align_result_init(&result);
    lrc_ctc_trellis_init(&trellis);
    lrc_ctc_path_init(&path);
    lrc_ctc_token_spans_init(&spans);
    lrc_ctc_word_spans_init(&word_spans);

    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_NONE);
    ASSERT(strequal(result.message, "ok"));
    ASSERT(result.frame_index == -1);
    ASSERT(result.token_index == -1);

    ASSERT(trellis.scores == NULL);
    ASSERT(trellis.frame_count == 0);
    ASSERT(trellis.target_token_count == 0);
    ASSERT(trellis.column_count == 0);
    ASSERT(trellis.cell_count == 0);

    ASSERT(path.steps == NULL);
    ASSERT(path.step_count == 0);
    ASSERT(path.step_cap == 0);

    ASSERT(spans.spans == NULL);
    ASSERT(spans.span_count == 0);
    ASSERT(spans.span_cap == 0);

    ASSERT(word_spans.spans == NULL);
    ASSERT(word_spans.span_count == 0);
    ASSERT(word_spans.span_cap == 0);

    return 0;
}

static int32
ctc_align_test_allocate_initializes_to_negative_infinity(void) {
    LrcCtcAlignResult result;
    LrcCtcTrellis trellis;

    lrc_ctc_trellis_init(&trellis);
    if (!lrc_ctc_trellis_allocate(&trellis, 3, 2, &result)) {
        return ctc_align_test_fail("allocate 3x3 trellis");
    }

    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_NONE);
    ASSERT(trellis.frame_count == 3);
    ASSERT(trellis.target_token_count == 2);
    ASSERT(trellis.column_count == 3);
    ASSERT(trellis.cell_count == 9);
    for (int64 i = 0; i < trellis.cell_count; i += 1) {
        ASSERT(ctc_align_is_negative_infinity(trellis.scores[i]));
    }
    ASSERT(lrc_ctc_trellis_cell(&trellis, 0, 0) == trellis.scores);
    ASSERT(lrc_ctc_trellis_cell(&trellis, 2, 2)
           == trellis.scores + 8);
    ASSERT(lrc_ctc_trellis_cell(&trellis, -1, 0) == NULL);
    ASSERT(lrc_ctc_trellis_cell(&trellis, 0, -1) == NULL);
    ASSERT(lrc_ctc_trellis_cell(&trellis, 3, 0) == NULL);
    ASSERT(lrc_ctc_trellis_cell(&trellis, 0, 3) == NULL);

    lrc_ctc_trellis_destroy(&trellis);

    return 0;
}

static int32
ctc_align_test_rejects_invalid_dimensions(void) {
    LrcCtcAlignResult result;
    LrcCtcTrellis trellis;

    lrc_ctc_trellis_init(&trellis);
    if (lrc_ctc_trellis_allocate(NULL, 1, 1, &result)) {
        return ctc_align_test_fail("missing trellis accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT);

    if (lrc_ctc_trellis_allocate(&trellis, 0, 1, &result)) {
        return ctc_align_test_fail("zero frames accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_DIMENSIONS);
    ASSERT(result.frame_index == 0);
    ASSERT(result.token_index == 1);

    if (lrc_ctc_trellis_allocate(&trellis, 1, 0, &result)) {
        return ctc_align_test_fail("zero target tokens accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_DIMENSIONS);
    ASSERT(result.frame_index == 1);
    ASSERT(result.token_index == 0);

    if (lrc_ctc_trellis_allocate(&trellis, INT64_MAX, 2, &result)) {
        return ctc_align_test_fail("huge trellis accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_TOO_LARGE);

    ASSERT(trellis.scores == NULL);

    return 0;
}

static int32
ctc_align_test_prepare_initializes_start_column(void) {
    LrcCtcAlignResult result;
    LrcCtcTrellis trellis;
    LrcCtcEmissions emissions;
    float values[] = {
        -0.10f, -2.00f, -3.00f,
        -0.20f, -2.10f, -3.10f,
        -0.30f, -2.20f, -3.20f,
    };

    ctc_align_make_emissions(&emissions, values, 3, 3);
    lrc_ctc_trellis_init(&trellis);
    if (!lrc_ctc_trellis_prepare(&trellis,
                                 &emissions,
                                 2,
                                 0,
                                 &result)) {
        return ctc_align_test_fail("prepare trellis");
    }

    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_NONE);
    ASSERT(trellis.frame_count == 3);
    ASSERT(trellis.target_token_count == 2);
    ASSERT(trellis.column_count == 3);
    ASSERT(ctc_align_float_close(*lrc_ctc_trellis_cell(&trellis, 0, 0),
                                 -0.10f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(*lrc_ctc_trellis_cell(&trellis, 1, 0),
                                 -0.30f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(*lrc_ctc_trellis_cell(&trellis, 2, 0),
                                 -0.60f,
                                 0.00001f));
    ASSERT(ctc_align_is_negative_infinity(
               *lrc_ctc_trellis_cell(&trellis, 0, 1)));
    ASSERT(ctc_align_is_negative_infinity(
               *lrc_ctc_trellis_cell(&trellis, 0, 2)));
    ASSERT(ctc_align_is_negative_infinity(
               *lrc_ctc_trellis_cell(&trellis, 2, 1)));
    ASSERT(ctc_align_is_negative_infinity(
               *lrc_ctc_trellis_cell(&trellis, 2, 2)));

    lrc_ctc_trellis_destroy(&trellis);

    return 0;
}


static int32
ctc_align_test_forward_scores_simple_path(void) {
    LrcCtcAlignResult result;
    LrcCtcTrellis trellis;
    LrcCtcEmissions emissions;
    int32 target_token_ids[] = {1, 2};
    float values[] = {
        -0.10f, -5.00f, -5.00f,
        -5.00f, -0.10f, -5.00f,
        -5.00f, -5.00f, -0.10f,
        -0.10f, -5.00f, -5.00f,
    };

    ctc_align_make_emissions(&emissions, values, 4, 3);
    lrc_ctc_trellis_init(&trellis);
    if (!lrc_ctc_trellis_score_forward(&trellis,
                                        &emissions,
                                        target_token_ids,
                                        2,
                                        0,
                                        &result)) {
        return ctc_align_test_fail("score simple forward path");
    }

    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_NONE);
    ASSERT(ctc_align_float_close(*lrc_ctc_trellis_cell(&trellis, 1, 1),
                                 -0.20f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(*lrc_ctc_trellis_cell(&trellis, 2, 2),
                                 -0.30f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(*lrc_ctc_trellis_cell(&trellis, 3, 2),
                                 -0.40f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(*lrc_ctc_trellis_cell(&trellis, 3, 0),
                                 -10.20f,
                                 0.00001f));

    lrc_ctc_trellis_destroy(&trellis);

    return 0;
}

static int32
ctc_align_test_forward_prefers_blank_stay(void) {
    LrcCtcAlignResult result;
    LrcCtcTrellis trellis;
    LrcCtcEmissions emissions;
    int32 target_token_ids[] = {1};
    float values[] = {
        -0.10f, -5.00f,
        -5.00f, -0.20f,
        -0.10f, -3.00f,
    };

    ctc_align_make_emissions(&emissions, values, 3, 2);
    lrc_ctc_trellis_init(&trellis);
    if (!lrc_ctc_trellis_score_forward(&trellis,
                                        &emissions,
                                        target_token_ids,
                                        1,
                                        0,
                                        &result)) {
        return ctc_align_test_fail("score blank stay path");
    }

    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_NONE);
    ASSERT(ctc_align_float_close(*lrc_ctc_trellis_cell(&trellis, 1, 1),
                                 -0.30f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(*lrc_ctc_trellis_cell(&trellis, 2, 1),
                                 -0.40f,
                                 0.00001f));

    lrc_ctc_trellis_destroy(&trellis);

    return 0;
}

static int32
ctc_align_test_forward_rejects_bad_targets(void) {
    LrcCtcAlignResult result;
    LrcCtcTrellis trellis;
    LrcCtcEmissions emissions;
    int32 target_token_ids[] = {1, 3};
    float values[] = {
        -0.10f, -0.20f,
        -0.30f, -0.40f,
    };

    ctc_align_make_emissions(&emissions, values, 2, 2);
    lrc_ctc_trellis_init(&trellis);
    if (lrc_ctc_trellis_score_forward(&trellis,
                                      &emissions,
                                      NULL,
                                      1,
                                      0,
                                      &result)) {
        return ctc_align_test_fail("missing target ids accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT);

    if (lrc_ctc_trellis_score_forward(&trellis,
                                      &emissions,
                                      target_token_ids,
                                      2,
                                      0,
                                      &result)) {
        return ctc_align_test_fail("bad target id accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_TARGET_TOKEN);
    ASSERT(result.token_index == 1);
    ASSERT(trellis.scores == NULL);

    return 0;
}


static int32
ctc_align_test_backtracks_simple_path(void) {
    LrcCtcAlignResult result;
    LrcCtcTrellis trellis;
    LrcCtcEmissions emissions;
    LrcCtcPath path;
    int32 target_token_ids[] = {1, 2};
    float values[] = {
        -0.10f, -5.00f, -5.00f,
        -5.00f, -0.10f, -5.00f,
        -5.00f, -5.00f, -0.10f,
        -0.10f, -5.00f, -5.00f,
    };

    ctc_align_make_emissions(&emissions, values, 4, 3);
    lrc_ctc_trellis_init(&trellis);
    lrc_ctc_path_init(&path);
    if (!lrc_ctc_trellis_score_forward(&trellis,
                                        &emissions,
                                        target_token_ids,
                                        2,
                                        0,
                                        &result)) {
        return ctc_align_test_fail("score simple backtrack path");
    }
    if (!lrc_ctc_trellis_backtrack(&trellis,
                                   &emissions,
                                   target_token_ids,
                                   2,
                                   0,
                                   &path,
                                   &result)) {
        return ctc_align_test_fail("backtrack simple path");
    }

    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_NONE);
    ASSERT(path.step_count == 4);
    ASSERT(path.steps[0].frame_index == 0);
    ASSERT(path.steps[0].is_blank);
    ASSERT(path.steps[0].token_id == 0);
    ASSERT(path.steps[1].frame_index == 1);
    ASSERT(!path.steps[1].is_blank);
    ASSERT(path.steps[1].token_index == 0);
    ASSERT(path.steps[1].token_id == 1);
    ASSERT(path.steps[2].frame_index == 2);
    ASSERT(!path.steps[2].is_blank);
    ASSERT(path.steps[2].token_index == 1);
    ASSERT(path.steps[2].token_id == 2);
    ASSERT(path.steps[3].frame_index == 3);
    ASSERT(path.steps[3].is_blank);
    ASSERT(path.steps[3].token_id == 0);

    lrc_ctc_path_destroy(&path);
    lrc_ctc_trellis_destroy(&trellis);

    return 0;
}

static int32
ctc_align_test_backtracks_repeated_tokens(void) {
    LrcCtcAlignResult result;
    LrcCtcTrellis trellis;
    LrcCtcEmissions emissions;
    LrcCtcPath path;
    int32 target_token_ids[] = {1, 1};
    float values[] = {
        -0.10f, -5.00f,
        -5.00f, -0.10f,
        -5.00f, -0.10f,
        -0.10f, -5.00f,
    };

    ctc_align_make_emissions(&emissions, values, 4, 2);
    lrc_ctc_trellis_init(&trellis);
    lrc_ctc_path_init(&path);
    if (!lrc_ctc_trellis_score_forward(&trellis,
                                        &emissions,
                                        target_token_ids,
                                        2,
                                        0,
                                        &result)) {
        return ctc_align_test_fail("score repeated-token path");
    }
    if (!lrc_ctc_trellis_backtrack(&trellis,
                                   &emissions,
                                   target_token_ids,
                                   2,
                                   0,
                                   &path,
                                   &result)) {
        return ctc_align_test_fail("backtrack repeated-token path");
    }

    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_NONE);
    ASSERT(path.step_count == 4);
    ASSERT(path.steps[1].frame_index == 1);
    ASSERT(!path.steps[1].is_blank);
    ASSERT(path.steps[1].token_index == 0);
    ASSERT(path.steps[1].token_id == 1);
    ASSERT(path.steps[2].frame_index == 2);
    ASSERT(!path.steps[2].is_blank);
    ASSERT(path.steps[2].token_index == 1);
    ASSERT(path.steps[2].token_id == 1);

    lrc_ctc_path_destroy(&path);
    lrc_ctc_trellis_destroy(&trellis);

    return 0;
}

static int32
ctc_align_test_backtrack_rejects_impossible_alignment(void) {
    LrcCtcAlignResult result;
    LrcCtcTrellis trellis;
    LrcCtcEmissions emissions;
    LrcCtcPath path;
    int32 target_token_ids[] = {1, 2};
    float values[] = {
        -0.10f, -5.00f, -5.00f,
        -5.00f, -0.10f, -5.00f,
    };

    ctc_align_make_emissions(&emissions, values, 2, 3);
    lrc_ctc_trellis_init(&trellis);
    lrc_ctc_path_init(&path);
    if (!lrc_ctc_trellis_score_forward(&trellis,
                                        &emissions,
                                        target_token_ids,
                                        2,
                                        0,
                                        &result)) {
        return ctc_align_test_fail("score impossible path");
    }
    if (lrc_ctc_trellis_backtrack(&trellis,
                                  &emissions,
                                  target_token_ids,
                                  2,
                                  0,
                                  &path,
                                  &result)) {
        return ctc_align_test_fail("impossible path accepted");
    }

    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_IMPOSSIBLE_ALIGNMENT);
    ASSERT(path.steps == NULL);
    ASSERT(path.step_count == 0);
    ASSERT(path.step_cap == 0);

    lrc_ctc_trellis_destroy(&trellis);

    return 0;
}

static int32
ctc_align_test_backtrack_rejects_invalid_trellis(void) {
    LrcCtcAlignResult result;
    LrcCtcTrellis trellis;
    LrcCtcEmissions emissions;
    LrcCtcPath path;
    int32 target_token_ids[] = {1};
    float values[] = {
        -0.10f, -5.00f,
        -5.00f, -0.10f,
    };

    ctc_align_make_emissions(&emissions, values, 2, 2);
    lrc_ctc_trellis_init(&trellis);
    lrc_ctc_path_init(&path);
    if (lrc_ctc_trellis_backtrack(&trellis,
                                  &emissions,
                                  target_token_ids,
                                  1,
                                  0,
                                  &path,
                                  &result)) {
        return ctc_align_test_fail("unscored trellis accepted");
    }

    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_TRELLIS);
    ASSERT(path.steps == NULL);

    return 0;
}

static int32
ctc_align_test_token_spans_from_backtracked_path(void) {
    LrcCtcAlignResult result;
    LrcCtcTrellis trellis;
    LrcCtcEmissions emissions;
    LrcCtcPath path;
    LrcCtcTokenSpans spans;
    int32 target_token_ids[] = {1, 2};
    float values[] = {
        -0.10f, -5.00f, -5.00f,
        -5.00f, -0.10f, -5.00f,
        -5.00f, -5.00f, -0.20f,
        -0.10f, -5.00f, -5.00f,
    };

    ctc_align_make_emissions(&emissions, values, 4, 3);
    lrc_ctc_trellis_init(&trellis);
    lrc_ctc_path_init(&path);
    lrc_ctc_token_spans_init(&spans);
    if (!lrc_ctc_trellis_score_forward(&trellis,
                                        &emissions,
                                        target_token_ids,
                                        2,
                                        0,
                                        &result)) {
        return ctc_align_test_fail("score span path");
    }
    if (!lrc_ctc_trellis_backtrack(&trellis,
                                   &emissions,
                                   target_token_ids,
                                   2,
                                   0,
                                   &path,
                                   &result)) {
        return ctc_align_test_fail("backtrack span path");
    }
    if (!lrc_ctc_path_to_token_spans(&path,
                                     &emissions,
                                     0.5f,
                                     &spans,
                                     &result)) {
        return ctc_align_test_fail("path to token spans");
    }

    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_NONE);
    ASSERT(spans.span_count == 2);
    ASSERT(spans.spans[0].token_index == 0);
    ASSERT(spans.spans[0].token_id == 1);
    ASSERT(spans.spans[0].start_frame == 1);
    ASSERT(spans.spans[0].end_frame == 2);
    ASSERT(ctc_align_float_close(spans.spans[0].start_seconds,
                                 0.5f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(spans.spans[0].end_seconds,
                                 1.0f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(spans.spans[0].score,
                                 -0.10f,
                                 0.00001f));
    ASSERT(spans.spans[1].token_index == 1);
    ASSERT(spans.spans[1].token_id == 2);
    ASSERT(spans.spans[1].start_frame == 2);
    ASSERT(spans.spans[1].end_frame == 3);
    ASSERT(ctc_align_float_close(spans.spans[1].start_seconds,
                                 1.0f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(spans.spans[1].end_seconds,
                                 1.5f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(spans.spans[1].score,
                                 -0.20f,
                                 0.00001f));

    lrc_ctc_token_spans_destroy(&spans);
    lrc_ctc_path_destroy(&path);
    lrc_ctc_trellis_destroy(&trellis);

    return 0;
}

static int32
ctc_align_test_token_spans_preserve_repeated_tokens(void) {
    LrcCtcAlignResult result;
    LrcCtcTrellis trellis;
    LrcCtcEmissions emissions;
    LrcCtcPath path;
    LrcCtcTokenSpans spans;
    int32 target_token_ids[] = {1, 1};
    float values[] = {
        -0.10f, -5.00f,
        -5.00f, -0.10f,
        -5.00f, -0.20f,
        -0.10f, -5.00f,
    };

    ctc_align_make_emissions(&emissions, values, 4, 2);
    lrc_ctc_trellis_init(&trellis);
    lrc_ctc_path_init(&path);
    lrc_ctc_token_spans_init(&spans);
    if (!lrc_ctc_trellis_score_forward(&trellis,
                                        &emissions,
                                        target_token_ids,
                                        2,
                                        0,
                                        &result)) {
        return ctc_align_test_fail("score repeated spans");
    }
    if (!lrc_ctc_trellis_backtrack(&trellis,
                                   &emissions,
                                   target_token_ids,
                                   2,
                                   0,
                                   &path,
                                   &result)) {
        return ctc_align_test_fail("backtrack repeated spans");
    }
    if (!lrc_ctc_path_to_token_spans(&path,
                                     &emissions,
                                     0.25f,
                                     &spans,
                                     &result)) {
        return ctc_align_test_fail("repeated path to spans");
    }

    ASSERT(spans.span_count == 2);
    ASSERT(spans.spans[0].token_id == 1);
    ASSERT(spans.spans[1].token_id == 1);
    ASSERT(spans.spans[0].token_index == 0);
    ASSERT(spans.spans[1].token_index == 1);
    ASSERT(spans.spans[0].start_frame == 1);
    ASSERT(spans.spans[1].start_frame == 2);
    ASSERT(ctc_align_float_close(spans.spans[0].start_seconds,
                                 0.25f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(spans.spans[1].start_seconds,
                                 0.50f,
                                 0.00001f));

    lrc_ctc_token_spans_destroy(&spans);
    lrc_ctc_path_destroy(&path);
    lrc_ctc_trellis_destroy(&trellis);

    return 0;
}

static int32
ctc_align_test_token_spans_collapse_contiguous_steps(void) {
    LrcCtcAlignResult result;
    LrcCtcPath path;
    LrcCtcEmissions emissions;
    LrcCtcTokenSpans spans;
    float values[] = {
        -5.00f, -0.20f, -5.00f,
        -5.00f, -0.40f, -5.00f,
        -0.10f, -5.00f, -5.00f,
        -5.00f, -5.00f, -0.30f,
    };

    ctc_align_make_emissions(&emissions, values, 4, 3);
    lrc_ctc_path_init(&path);
    lrc_ctc_token_spans_init(&spans);
    if (!lrc_ctc_path_allocate(&path, 4, &result)) {
        return ctc_align_test_fail("allocate manual span path");
    }

    lrc_ctc_path_set_token_step(&path, 0, 1, 1);
    lrc_ctc_path_set_token_step(&path, 1, 1, 1);
    lrc_ctc_path_set_blank_step(&path, 2, 1, 0);
    lrc_ctc_path_set_token_step(&path, 3, 2, 2);
    if (!lrc_ctc_path_to_token_spans(&path,
                                     &emissions,
                                     0.1f,
                                     &spans,
                                     &result)) {
        return ctc_align_test_fail("collapse contiguous spans");
    }

    ASSERT(spans.span_count == 2);
    ASSERT(spans.spans[0].token_index == 0);
    ASSERT(spans.spans[0].token_id == 1);
    ASSERT(spans.spans[0].start_frame == 0);
    ASSERT(spans.spans[0].end_frame == 2);
    ASSERT(ctc_align_float_close(spans.spans[0].start_seconds,
                                 0.0f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(spans.spans[0].end_seconds,
                                 0.2f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(spans.spans[0].score,
                                 -0.30f,
                                 0.00001f));
    ASSERT(spans.spans[1].token_index == 1);
    ASSERT(spans.spans[1].token_id == 2);
    ASSERT(spans.spans[1].start_frame == 3);
    ASSERT(spans.spans[1].end_frame == 4);

    lrc_ctc_token_spans_destroy(&spans);
    lrc_ctc_path_destroy(&path);

    return 0;
}

static int32
ctc_align_test_token_spans_reject_bad_inputs(void) {
    LrcCtcAlignResult result;
    LrcCtcPath path;
    LrcCtcEmissions emissions;
    LrcCtcTokenSpans spans;
    float values[] = {
        -0.10f, -5.00f,
        -5.00f, -0.20f,
    };

    ctc_align_make_emissions(&emissions, values, 2, 2);
    lrc_ctc_path_init(&path);
    lrc_ctc_token_spans_init(&spans);
    if (lrc_ctc_path_to_token_spans(NULL,
                                    &emissions,
                                    0.1f,
                                    &spans,
                                    &result)) {
        return ctc_align_test_fail("missing path accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT);

    if (lrc_ctc_path_to_token_spans(&path,
                                    &emissions,
                                    0.1f,
                                    &spans,
                                    &result)) {
        return ctc_align_test_fail("empty path accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_PATH);

    if (!lrc_ctc_path_allocate(&path, 2, &result)) {
        return ctc_align_test_fail("allocate invalid span path");
    }
    lrc_ctc_path_set_blank_step(&path, 0, 0, 0);
    lrc_ctc_path_set_token_step(&path, 1, 1, 1);
    if (lrc_ctc_path_to_token_spans(&path,
                                    &emissions,
                                    0.0f,
                                    &spans,
                                    &result)) {
        return ctc_align_test_fail("zero frame duration accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_FRAME_DURATION);

    path.steps[1].token_id = 2;
    if (lrc_ctc_path_to_token_spans(&path,
                                    &emissions,
                                    0.1f,
                                    &spans,
                                    &result)) {
        return ctc_align_test_fail("bad path token id accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_PATH);
    ASSERT(spans.spans == NULL);

    lrc_ctc_path_destroy(&path);

    return 0;
}


static int32
ctc_align_test_word_spans_group_generated_words(void) {
    LrcLyrics lyrics;
    LrcLyricsNormalized normalized;
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizedText tokens;
    LrcCtcTokenSpans token_spans;
    LrcCtcWordSpans word_spans;
    LrcCtcAlignResult result;
    char text[] = "Hi, Bob!\nNext line\n";

    lrc_ctc_token_spans_init(&token_spans);
    lrc_ctc_word_spans_init(&word_spans);
    if (!ctc_align_load_tokenized_lyrics(text,
                                         strlen32(text),
                                         &lyrics,
                                         &normalized,
                                         &tokenizer,
                                         &tokens)) {
        return ctc_align_test_fail("load generated word lyrics");
    }
    if (!ctc_align_make_token_spans_from_tokens(&tokens,
                                                0.0f,
                                                0.10f,
                                                &token_spans)) {
        return ctc_align_test_fail("make generated token spans");
    }
    if (!lrc_ctc_token_spans_to_word_spans(&token_spans,
                                           &tokens,
                                           &normalized,
                                           &word_spans,
                                           &result)) {
        return ctc_align_test_fail("convert generated word spans");
    }

    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_NONE);
    ASSERT(word_spans.span_count == 4);
    ctc_align_assert_word_text(&normalized,
                               word_spans.spans + 0,
                               STRLIT("hi"));
    ctc_align_assert_word_text(&normalized,
                               word_spans.spans + 1,
                               STRLIT("bob"));
    ctc_align_assert_word_text(&normalized,
                               word_spans.spans + 2,
                               STRLIT("next"));
    ctc_align_assert_word_text(&normalized,
                               word_spans.spans + 3,
                               STRLIT("line"));
    ASSERT(word_spans.spans[0].line_index == 0);
    ASSERT(word_spans.spans[1].line_index == 0);
    ASSERT(word_spans.spans[2].line_index == 1);
    ASSERT(word_spans.spans[3].line_index == 1);
    ASSERT(word_spans.spans[0].token_start_index == 0);
    ASSERT(word_spans.spans[0].token_end_index == 2);
    ASSERT(word_spans.spans[1].token_start_index == 3);
    ASSERT(word_spans.spans[1].token_end_index == 6);
    ASSERT(ctc_align_float_close(word_spans.spans[0].start_seconds,
                                 0.0f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(word_spans.spans[0].end_seconds,
                                 0.2f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(word_spans.spans[1].start_seconds,
                                 0.3f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(word_spans.spans[2].start_seconds,
                                 0.7f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(word_spans.spans[0].score,
                                 -0.105f,
                                 0.00001f));

    lrc_ctc_word_spans_destroy(&word_spans);
    lrc_ctc_token_spans_destroy(&token_spans);
    lrc_ctc_tokenized_text_destroy(&tokens);
    lrc_ctc_tokenizer_destroy(&tokenizer);
    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);

    return 0;
}

static int32
ctc_align_test_word_spans_handle_removed_punctuation(void) {
    LrcLyrics lyrics;
    LrcLyricsNormalized normalized;
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizedText tokens;
    LrcCtcTokenSpans token_spans;
    LrcCtcWordSpans word_spans;
    LrcCtcAlignResult result;
    char text[] = "A---B   C\n";

    lrc_ctc_token_spans_init(&token_spans);
    lrc_ctc_word_spans_init(&word_spans);
    if (!ctc_align_load_tokenized_lyrics(text,
                                         strlen32(text),
                                         &lyrics,
                                         &normalized,
                                         &tokenizer,
                                         &tokens)) {
        return ctc_align_test_fail("load punctuation word lyrics");
    }
    ASSERT(strequal2(normalized.text, normalized.text_len, "ab c", 4));
    if (!ctc_align_make_token_spans_from_tokens(&tokens,
                                                0.5f,
                                                0.25f,
                                                &token_spans)) {
        return ctc_align_test_fail("make punctuation token spans");
    }
    if (!lrc_ctc_token_spans_to_word_spans(&token_spans,
                                           &tokens,
                                           &normalized,
                                           &word_spans,
                                           &result)) {
        return ctc_align_test_fail("convert punctuation word spans");
    }

    ASSERT(word_spans.span_count == 2);
    ctc_align_assert_word_text(&normalized,
                               word_spans.spans + 0,
                               STRLIT("ab"));
    ctc_align_assert_word_text(&normalized,
                               word_spans.spans + 1,
                               STRLIT("c"));
    ASSERT(word_spans.spans[0].line_index == 0);
    ASSERT(word_spans.spans[1].line_index == 0);
    ASSERT(ctc_align_float_close(word_spans.spans[0].start_seconds,
                                 0.5f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(word_spans.spans[0].end_seconds,
                                 1.0f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(word_spans.spans[1].start_seconds,
                                 1.25f,
                                 0.00001f));

    lrc_ctc_word_spans_destroy(&word_spans);
    lrc_ctc_token_spans_destroy(&token_spans);
    lrc_ctc_tokenized_text_destroy(&tokens);
    lrc_ctc_tokenizer_destroy(&tokenizer);
    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);

    return 0;
}

static int32
ctc_align_test_word_spans_reject_bad_inputs(void) {
    LrcLyrics lyrics;
    LrcLyricsNormalized normalized;
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizedText tokens;
    LrcCtcTokenSpans token_spans;
    LrcCtcWordSpans word_spans;
    LrcCtcAlignResult result;
    char text[] = "a b\n";

    lrc_ctc_token_spans_init(&token_spans);
    lrc_ctc_word_spans_init(&word_spans);
    if (lrc_ctc_token_spans_to_word_spans(NULL,
                                          &tokens,
                                          &normalized,
                                          &word_spans,
                                          &result)) {
        return ctc_align_test_fail("missing token spans accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT);

    if (!ctc_align_load_tokenized_lyrics(text,
                                         strlen32(text),
                                         &lyrics,
                                         &normalized,
                                         &tokenizer,
                                         &tokens)) {
        return ctc_align_test_fail("load bad-input word lyrics");
    }
    if (!ctc_align_make_token_spans_from_tokens(&tokens,
                                                0.0f,
                                                0.10f,
                                                &token_spans)) {
        return ctc_align_test_fail("make bad-input token spans");
    }

    token_spans.span_count -= 1;
    if (lrc_ctc_token_spans_to_word_spans(&token_spans,
                                          &tokens,
                                          &normalized,
                                          &word_spans,
                                          &result)) {
        return ctc_align_test_fail("short token spans accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_TOKEN_SPANS);
    token_spans.span_count += 1;

    token_spans.spans[0].token_id = 999;
    if (lrc_ctc_token_spans_to_word_spans(&token_spans,
                                          &tokens,
                                          &normalized,
                                          &word_spans,
                                          &result)) {
        return ctc_align_test_fail("mismatched token span accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_TOKEN_SPANS);
    token_spans.spans[0].token_id = tokens.tokens[0].token_id;

    tokens.tokens[0].normalized_end = normalized.text_len;
    if (lrc_ctc_token_spans_to_word_spans(&token_spans,
                                          &tokens,
                                          &normalized,
                                          &word_spans,
                                          &result)) {
        return ctc_align_test_fail("mixed word/space token accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_TOKENIZED_TEXT);

    lrc_ctc_word_spans_destroy(&word_spans);
    lrc_ctc_token_spans_destroy(&token_spans);
    lrc_ctc_tokenized_text_destroy(&tokens);
    lrc_ctc_tokenizer_destroy(&tokenizer);
    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);

    return 0;
}

static int32
ctc_align_test_maxwell_word_line_mapping(void) {
    LrcLyrics lyrics;
    LrcLyricsNormalized normalized;
    LrcLyricsLoadResult lyrics_result;
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizedText tokens;
    LrcCtcTokenizeResult tokenize_result;
    LrcCtcTokenSpans token_spans;
    LrcCtcWordSpans word_spans;
    LrcCtcAlignResult result;
    char *lyrics_path;
    int32 expected_lines[] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 1, 1, 1, 1, 1, 1,
        2, 2, 2, 2, 2, 2,
        4, 4, 4, 4, 4,
        5, 5, 5, 5, 5,
    };

    lyrics_path = getenv("LRC_TEST_MAXWELL_TXT");
    if (lyrics_path == NULL) {
        return 0;
    }

    lrc_lyrics_init(&lyrics);
    lrc_lyrics_normalized_init(&normalized);
    lrc_ctc_tokenizer_init(&tokenizer);
    lrc_ctc_tokenized_text_init(&tokens);
    lrc_ctc_token_spans_init(&token_spans);
    lrc_ctc_word_spans_init(&word_spans);
    if (!lrc_lyrics_load_file(&lyrics, lyrics_path, &lyrics_result)) {
        return ctc_align_test_fail("load maxwell word lyrics");
    }
    if (!lrc_lyrics_normalize(&lyrics, &normalized)) {
        return ctc_align_test_fail("normalize maxwell word lyrics");
    }
    if (!ctc_align_load_alphabet_tokenizer(&tokenizer)) {
        return ctc_align_test_fail("load maxwell word tokenizer");
    }
    if (!lrc_ctc_tokenizer_tokenize_normalized(&tokenizer,
                                               &normalized,
                                               &tokens,
                                               &tokenize_result)) {
        return ctc_align_test_fail("tokenize maxwell word lyrics");
    }
    if (!ctc_align_make_token_spans_from_tokens(&tokens,
                                                0.0f,
                                                0.02f,
                                                &token_spans)) {
        return ctc_align_test_fail("make maxwell word token spans");
    }
    if (!lrc_ctc_token_spans_to_word_spans(&token_spans,
                                           &tokens,
                                           &normalized,
                                           &word_spans,
                                           &result)) {
        return ctc_align_test_fail("convert maxwell word spans");
    }

    ASSERT(word_spans.span_count == LENGTH(expected_lines));
    for (int64 i = 0; i < word_spans.span_count; i += 1) {
        LrcCtcWordSpan *word;
        int32 line_start;
        int32 line_end;

        word = word_spans.spans + i;
        ASSERT(word->line_index == expected_lines[i]);
        ASSERT(lrc_lyrics_normalized_line_range(&normalized,
                                                word->line_index,
                                                &line_start,
                                                &line_end));
        ASSERT(word->normalized_start >= line_start);
        ASSERT(word->normalized_end <= line_end);
    }
    ctc_align_assert_word_text(&normalized,
                               word_spans.spans + 0,
                               STRLIT("can"));
    ctc_align_assert_word_text(&normalized,
                               word_spans.spans + 22,
                               STRLIT("bang"));
    ctc_align_assert_word_text(&normalized,
                               word_spans.spans + 27,
                               STRLIT("came"));

    lrc_ctc_word_spans_destroy(&word_spans);
    lrc_ctc_token_spans_destroy(&token_spans);
    lrc_ctc_tokenized_text_destroy(&tokens);
    lrc_ctc_tokenizer_destroy(&tokenizer);
    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);

    return 0;
}


static int32
ctc_align_test_full_synthetic_alignment_pipeline(void) {
    LrcLyrics lyrics;
    LrcLyricsNormalized normalized;
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizedText tokens;
    LrcCtcTokenizeResult tokenize_result;
    LrcCtcAlignResult align_result;
    LrcCtcTrellis trellis;
    LrcCtcPath path;
    LrcCtcTokenSpans spans;
    LrcCtcEmissions emissions;
    int32 *target_token_ids;
    float *values;
    char text[] = "AB, cab!\n";
    float frame_duration_seconds;
    int64 token_count;
    int64 frame_count;
    int64 vocabulary_size;
    int64 value_count;

    lrc_lyrics_normalized_init(&normalized);
    lrc_ctc_tokenizer_init(&tokenizer);
    lrc_ctc_tokenized_text_init(&tokens);
    lrc_ctc_trellis_init(&trellis);
    lrc_ctc_path_init(&path);
    lrc_ctc_token_spans_init(&spans);
    if (!ctc_align_load_lyrics_text(&lyrics, text, strlen32(text))) {
        return ctc_align_test_fail("load synthetic lyrics");
    }
    if (!lrc_lyrics_normalize(&lyrics, &normalized)) {
        return ctc_align_test_fail("normalize synthetic lyrics");
    }
    ASSERT(strequal2(normalized.text, normalized.text_len, "ab cab", 6));

    if (!ctc_align_load_alphabet_tokenizer(&tokenizer)) {
        return ctc_align_test_fail("load synthetic tokenizer");
    }
    if (!lrc_ctc_tokenizer_tokenize_normalized(&tokenizer,
                                               &normalized,
                                               &tokens,
                                               &tokenize_result)) {
        return ctc_align_test_fail("tokenize synthetic lyrics");
    }

    ASSERT(tokens.token_count == normalized.text_len);
    for (int32 i = 0; i < tokens.token_count; i += 1) {
        ASSERT(tokens.tokens[i].normalized_start == i);
        ASSERT(tokens.tokens[i].normalized_end == i + 1);
        ASSERT(tokens.tokens[i].line_index == 0);
    }

    token_count = tokens.token_count;
    frame_count = token_count + 2;
    vocabulary_size = tokenizer.token_count;
    value_count = frame_count*vocabulary_size;
    target_token_ids = malloc2(token_count*SIZEOF(*target_token_ids));
    values = malloc2(value_count*SIZEOF(*values));
    for (int64 i = 0; i < token_count; i += 1) {
        target_token_ids[i] = tokens.tokens[i].token_id;
    }
    ctc_align_fill_predictable_values(values,
                                      frame_count,
                                      vocabulary_size,
                                      tokenizer.blank_id,
                                      target_token_ids,
                                      token_count);
    ctc_align_make_emissions(&emissions, values, frame_count, vocabulary_size);
    if (!lrc_ctc_trellis_score_forward(&trellis,
                                        &emissions,
                                        target_token_ids,
                                        token_count,
                                        tokenizer.blank_id,
                                        &align_result)) {
        return ctc_align_test_fail("score synthetic full path");
    }
    if (!lrc_ctc_trellis_backtrack(&trellis,
                                   &emissions,
                                   target_token_ids,
                                   token_count,
                                   tokenizer.blank_id,
                                   &path,
                                   &align_result)) {
        return ctc_align_test_fail("backtrack synthetic full path");
    }

    frame_duration_seconds = 0.125f;
    if (!lrc_ctc_path_to_token_spans(&path,
                                     &emissions,
                                     frame_duration_seconds,
                                     &spans,
                                     &align_result)) {
        return ctc_align_test_fail("span synthetic full path");
    }

    ASSERT(align_result.error == LRC_CTC_ALIGN_ERROR_NONE);
    ASSERT(spans.span_count == token_count);
    for (int64 i = 0; i < spans.span_count; i += 1) {
        float expected_start;
        float expected_end;

        expected_start = (float)(i + 1)*frame_duration_seconds;
        expected_end = (float)(i + 2)*frame_duration_seconds;
        ASSERT(spans.spans[i].token_index == i);
        ASSERT(spans.spans[i].token_id == target_token_ids[i]);
        ASSERT(spans.spans[i].start_frame == i + 1);
        ASSERT(spans.spans[i].end_frame == i + 2);
        ASSERT(ctc_align_float_close(spans.spans[i].start_seconds,
                                     expected_start,
                                     0.00001f));
        ASSERT(ctc_align_float_close(spans.spans[i].end_seconds,
                                     expected_end,
                                     0.00001f));
        ASSERT(ctc_align_float_close(spans.spans[i].score,
                                     -0.05f,
                                     0.00001f));
    }

    lrc_ctc_token_spans_destroy(&spans);
    lrc_ctc_path_destroy(&path);
    lrc_ctc_trellis_destroy(&trellis);
    free2(values, value_count*SIZEOF(*values));
    free2(target_token_ids, token_count*SIZEOF(*target_token_ids));
    lrc_ctc_tokenized_text_destroy(&tokens);
    lrc_ctc_tokenizer_destroy(&tokenizer);
    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);

    return 0;
}

static int32
ctc_align_test_maxwell_fake_token_timing(void) {
    LrcLyrics lyrics;
    LrcLyricsNormalized normalized;
    LrcLyricsLoadResult lyrics_result;
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizedText tokens;
    LrcCtcTokenizeResult tokenize_result;
    LrcCtcAlignResult align_result;
    LrcCtcTrellis trellis;
    LrcCtcPath path;
    LrcCtcTokenSpans spans;
    LrcCtcEmissions emissions;
    int32 *target_token_ids;
    float *values;
    char *lyrics_path;
    int64 token_count;
    int64 frame_count;
    int64 vocabulary_size;
    int64 value_count;

    lyrics_path = getenv("LRC_TEST_MAXWELL_TXT");
    if (lyrics_path == NULL) {
        return 0;
    }

    lrc_lyrics_init(&lyrics);
    lrc_lyrics_normalized_init(&normalized);
    lrc_ctc_tokenizer_init(&tokenizer);
    lrc_ctc_tokenized_text_init(&tokens);
    lrc_ctc_trellis_init(&trellis);
    lrc_ctc_path_init(&path);
    lrc_ctc_token_spans_init(&spans);
    if (!lrc_lyrics_load_file(&lyrics, lyrics_path, &lyrics_result)) {
        return ctc_align_test_fail("load maxwell lyrics");
    }
    if (!lrc_lyrics_normalize(&lyrics, &normalized)) {
        return ctc_align_test_fail("normalize maxwell lyrics");
    }
    if (!ctc_align_load_alphabet_tokenizer(&tokenizer)) {
        return ctc_align_test_fail("load maxwell test tokenizer");
    }
    if (!lrc_ctc_tokenizer_tokenize_normalized(&tokenizer,
                                               &normalized,
                                               &tokens,
                                               &tokenize_result)) {
        return ctc_align_test_fail("tokenize maxwell lyrics");
    }
    ASSERT(tokens.token_count > 0);

    token_count = tokens.token_count;
    frame_count = token_count + 2;
    vocabulary_size = tokenizer.token_count;
    value_count = frame_count*vocabulary_size;
    target_token_ids = malloc2(token_count*SIZEOF(*target_token_ids));
    values = malloc2(value_count*SIZEOF(*values));
    for (int64 i = 0; i < token_count; i += 1) {
        target_token_ids[i] = tokens.tokens[i].token_id;
    }
    ctc_align_fill_predictable_values(values,
                                      frame_count,
                                      vocabulary_size,
                                      tokenizer.blank_id,
                                      target_token_ids,
                                      token_count);
    ctc_align_make_emissions(&emissions, values, frame_count, vocabulary_size);
    if (!lrc_ctc_trellis_score_forward(&trellis,
                                        &emissions,
                                        target_token_ids,
                                        token_count,
                                        tokenizer.blank_id,
                                        &align_result)) {
        return ctc_align_test_fail("score maxwell fake path");
    }
    if (!lrc_ctc_trellis_backtrack(&trellis,
                                   &emissions,
                                   target_token_ids,
                                   token_count,
                                   tokenizer.blank_id,
                                   &path,
                                   &align_result)) {
        return ctc_align_test_fail("backtrack maxwell fake path");
    }
    if (!lrc_ctc_path_to_token_spans(&path,
                                     &emissions,
                                     0.02f,
                                     &spans,
                                     &align_result)) {
        return ctc_align_test_fail("maxwell fake path to spans");
    }

    ASSERT(spans.span_count == token_count);
    ASSERT(spans.spans[0].token_index == 0);
    ASSERT(spans.spans[0].start_frame == 1);
    ASSERT(ctc_align_float_close(spans.spans[0].start_seconds,
                                 0.02f,
                                 0.00001f));
    ASSERT(spans.spans[token_count - 1].token_index == token_count - 1);
    ASSERT(spans.spans[token_count - 1].start_frame == token_count);
    ASSERT(ctc_align_float_close(spans.spans[token_count - 1].start_seconds,
                                 (float)token_count*0.02f,
                                 0.0001f));

    lrc_ctc_token_spans_destroy(&spans);
    lrc_ctc_path_destroy(&path);
    lrc_ctc_trellis_destroy(&trellis);
    free2(values, value_count*SIZEOF(*values));
    free2(target_token_ids, token_count*SIZEOF(*target_token_ids));
    lrc_ctc_tokenized_text_destroy(&tokens);
    lrc_ctc_tokenizer_destroy(&tokenizer);
    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);

    return 0;
}

static int32
ctc_align_test_prepare_rejects_invalid_emissions(void) {
    LrcCtcAlignResult result;
    LrcCtcTrellis trellis;
    LrcCtcEmissions emissions;
    float values[] = {-0.1f, -0.2f};

    lrc_ctc_trellis_init(&trellis);
    if (lrc_ctc_trellis_prepare(&trellis, NULL, 1, 0, &result)) {
        return ctc_align_test_fail("missing emissions accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT);

    ctc_align_make_emissions(&emissions, values, 1, 2);
    emissions.value_count = 1;
    if (lrc_ctc_trellis_prepare(&trellis, &emissions, 1, 0, &result)) {
        return ctc_align_test_fail("bad emissions value count accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_EMISSIONS);

    ctc_align_make_emissions(&emissions, values, 1, 2);
    if (lrc_ctc_trellis_prepare(&trellis, &emissions, 1, 2, &result)) {
        return ctc_align_test_fail("bad blank token accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_BLANK_TOKEN);
    ASSERT(result.token_index == 2);

    ASSERT(trellis.scores == NULL);

    return 0;
}

int32
main(void) {
    if (ctc_align_test_empty_initializers() != 0) {
        exit(1);
    }
    if (ctc_align_test_allocate_initializes_to_negative_infinity() != 0) {
        exit(1);
    }
    if (ctc_align_test_rejects_invalid_dimensions() != 0) {
        exit(1);
    }
    if (ctc_align_test_prepare_initializes_start_column() != 0) {
        exit(1);
    }
    if (ctc_align_test_prepare_rejects_invalid_emissions() != 0) {
        exit(1);
    }
    if (ctc_align_test_forward_scores_simple_path() != 0) {
        exit(1);
    }
    if (ctc_align_test_forward_prefers_blank_stay() != 0) {
        exit(1);
    }
    if (ctc_align_test_forward_rejects_bad_targets() != 0) {
        exit(1);
    }
    if (ctc_align_test_backtracks_simple_path() != 0) {
        exit(1);
    }
    if (ctc_align_test_backtracks_repeated_tokens() != 0) {
        exit(1);
    }
    if (ctc_align_test_backtrack_rejects_impossible_alignment() != 0) {
        exit(1);
    }
    if (ctc_align_test_backtrack_rejects_invalid_trellis() != 0) {
        exit(1);
    }
    if (ctc_align_test_token_spans_from_backtracked_path() != 0) {
        exit(1);
    }
    if (ctc_align_test_token_spans_preserve_repeated_tokens() != 0) {
        exit(1);
    }
    if (ctc_align_test_token_spans_collapse_contiguous_steps() != 0) {
        exit(1);
    }
    if (ctc_align_test_token_spans_reject_bad_inputs() != 0) {
        exit(1);
    }
    if (ctc_align_test_word_spans_group_generated_words() != 0) {
        exit(1);
    }
    if (ctc_align_test_word_spans_handle_removed_punctuation() != 0) {
        exit(1);
    }
    if (ctc_align_test_word_spans_reject_bad_inputs() != 0) {
        exit(1);
    }
    if (ctc_align_test_full_synthetic_alignment_pipeline() != 0) {
        exit(1);
    }
    if (ctc_align_test_maxwell_fake_token_timing() != 0) {
        exit(1);
    }
    if (ctc_align_test_maxwell_word_line_mapping() != 0) {
        exit(1);
    }

    exit(0);
}

#endif /* TESTING_ctc_align */
