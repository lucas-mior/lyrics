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

    lrc_ctc_align_result_init(&result);
    lrc_ctc_trellis_init(&trellis);

    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_NONE);
    ASSERT(strequal(result.message, "ok"));
    ASSERT(result.frame_index == -1);
    ASSERT(result.token_index == -1);

    ASSERT(trellis.scores == NULL);
    ASSERT(trellis.frame_count == 0);
    ASSERT(trellis.target_token_count == 0);
    ASSERT(trellis.column_count == 0);
    ASSERT(trellis.cell_count == 0);

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

    exit(0);
}

#endif /* TESTING_ctc_align */
