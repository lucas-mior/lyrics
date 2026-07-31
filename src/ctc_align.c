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
lrc_ctc_trellis_emissions_ready(
    LrcCtcEmissions *emissions,
    int32 blank_token_id,
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

#if TESTING_ctc_align

#define CBASE_IMPLEMENT
#include "cbase.h"

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

static int32
ctc_align_test_empty_initializers(void) {
    LrcCtcAlignResult result;
    LrcCtcTrellis trellis;
    LrcCtcPath path;

    lrc_ctc_align_result_init(&result);
    lrc_ctc_trellis_init(&trellis);
    lrc_ctc_path_init(&path);

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

    exit(0);
}

#endif /* TESTING_ctc_align */
