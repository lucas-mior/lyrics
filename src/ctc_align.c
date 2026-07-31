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


enum LrcCtcAlignStateKind {
    LRC_CTC_ALIGN_STATE_BLANK,
    LRC_CTC_ALIGN_STATE_TOKEN,
};

typedef struct LrcCtcAlignState {
    enum LrcCtcAlignStateKind kind;

    int64 token_index;
    int32 token_id;
} LrcCtcAlignState;

typedef struct LrcCtcAlignGraph {
    LrcCtcAlignState *states;

    int64 state_count;
    int64 target_token_count;
} LrcCtcAlignGraph;

static void
lrc_ctc_align_graph_init(LrcCtcAlignGraph *graph) {
    if (graph == NULL) {
        return;
    }

    memset64(graph, 0, SIZEOF(*graph));

    return;
}

static void
lrc_ctc_align_graph_destroy(LrcCtcAlignGraph *graph) {
    if (graph == NULL) {
        return;
    }

    if (graph->states) {
        free2(graph->states, graph->state_count*SIZEOF(*graph->states));
    }

    lrc_ctc_align_graph_init(graph);

    return;
}

static bool
lrc_ctc_align_graph_state_count(
    int64 target_token_count,
    int64 *state_count,
    LrcCtcAlignResult *result
) {
    if (state_count == NULL) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT,
            "CTC graph state-count destination is missing",
            -1,
            -1
        );
        return false;
    }
    *state_count = 0;

    if (target_token_count <= 0) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_DIMENSIONS,
            "CTC graph target token count must be positive",
            -1,
            target_token_count
        );
        return false;
    }
    if (target_token_count > (INT64_MAX - 1)/2) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_TOO_LARGE,
            "CTC graph state count is too large",
            -1,
            target_token_count
        );
        return false;
    }

    *state_count = 2*target_token_count + 1;

    return true;
}

static int64
lrc_ctc_required_frame_count_for_tokens(
    int32 *target_token_ids,
    int64 target_token_count
) {
    int64 frame_count;

    if ((target_token_ids == NULL) || (target_token_count <= 0)) {
        return -1;
    }

    frame_count = target_token_count;
    for (int64 i = 1; i < target_token_count; i += 1) {
        if (target_token_ids[i] != target_token_ids[i - 1]) {
            continue;
        }
        if (frame_count >= INT64_MAX) {
            return -1;
        }
        frame_count += 1;
    }

    return frame_count;
}

static bool
lrc_ctc_align_graph_build(
    LrcCtcAlignGraph *graph,
    int32 *target_token_ids,
    int64 target_token_count,
    LrcCtcAlignResult *result
) {
    int64 state_count;

    if (result) {
        lrc_ctc_align_result_init(result);
    }
    if (graph == NULL) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT,
            "CTC graph destination is missing",
            -1,
            -1
        );
        return false;
    }
    if (target_token_ids == NULL) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT,
            "CTC graph target token ids are missing",
            -1,
            -1
        );
        return false;
    }

    lrc_ctc_align_graph_destroy(graph);
    if (!lrc_ctc_align_graph_state_count(target_token_count,
                                          &state_count,
                                          result)) {
        return false;
    }
    if (state_count > INT64_MAX/SIZEOF(*graph->states)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_TOO_LARGE,
            "CTC graph state allocation is too large",
            -1,
            state_count
        );
        return false;
    }

    graph->states = malloc2(state_count*SIZEOF(*graph->states));
    graph->state_count = state_count;
    graph->target_token_count = target_token_count;

    for (int64 i = 0; i < graph->state_count; i += 1) {
        LrcCtcAlignState *state = graph->states + i;

        state->kind = LRC_CTC_ALIGN_STATE_BLANK;
        state->token_index = -1;
        state->token_id = -1;

        if ((i & 1) == 0) {
            continue;
        }

        state->kind = LRC_CTC_ALIGN_STATE_TOKEN;
        state->token_index = i/2;
        state->token_id = target_token_ids[state->token_index];
    }

    return true;
}

static bool
lrc_ctc_align_graph_state_valid(
    LrcCtcAlignGraph *graph,
    int64 state_index
) {
    if (graph == NULL) {
        return false;
    }
    if (graph->states == NULL) {
        return false;
    }
    if ((state_index < 0) || (state_index >= graph->state_count)) {
        return false;
    }

    return true;
}

static bool
lrc_ctc_align_state_can_skip(
    LrcCtcAlignGraph *graph,
    int64 from_state,
    int64 to_state
) {
    LrcCtcAlignState *from;
    LrcCtcAlignState *to;

    if (!lrc_ctc_align_graph_state_valid(graph, from_state)
        || !lrc_ctc_align_graph_state_valid(graph, to_state)) {
        return false;
    }
    if (to_state != from_state + 2) {
        return false;
    }

    from = graph->states + from_state;
    to = graph->states + to_state;
    if ((from->kind != LRC_CTC_ALIGN_STATE_TOKEN)
        || (to->kind != LRC_CTC_ALIGN_STATE_TOKEN)) {
        return false;
    }

    return from->token_id != to->token_id;
}

static bool
lrc_ctc_align_graph_transition_allowed(
    LrcCtcAlignGraph *graph,
    int64 from_state,
    int64 to_state
) {
    if (!lrc_ctc_align_graph_state_valid(graph, from_state)
        || !lrc_ctc_align_graph_state_valid(graph, to_state)) {
        return false;
    }
    if (to_state == from_state) {
        return true;
    }
    if (to_state == from_state + 1) {
        return true;
    }

    return lrc_ctc_align_state_can_skip(graph, from_state, to_state);
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
    if (trellis->previous_states) {
        free2(trellis->previous_states,
              trellis->cell_count*SIZEOF(*trellis->previous_states));
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

static void
lrc_ctc_line_timestamps_init(LrcCtcLineTimestamps *timestamps) {
    if (timestamps == NULL) {
        return;
    }

    memset64(timestamps, 0, SIZEOF(*timestamps));

    return;
}

static void
lrc_ctc_line_timestamps_destroy(LrcCtcLineTimestamps *timestamps) {
    if (timestamps == NULL) {
        return;
    }

    if (timestamps->lines) {
        free2(timestamps->lines,
              timestamps->line_cap*SIZEOF(*timestamps->lines));
    }

    lrc_ctc_line_timestamps_init(timestamps);

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
        path->steps[i].state_index = -1;
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
    int64 state_count;
    int64 cells;

    if (frame_count <= 0) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_DIMENSIONS,
            "CTC trellis frame count must be positive",
            frame_count,
            target_token_count
        );
        return false;
    }
    if (!lrc_ctc_align_graph_state_count(target_token_count,
                                          &state_count,
                                          result)) {
        return false;
    }

    if (frame_count > INT64_MAX/state_count) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_TOO_LARGE,
            "CTC trellis cell count is too large",
            frame_count,
            state_count
        );
        return false;
    }

    cells = frame_count*state_count;

    *column_count = state_count;
    *cell_count = cells;

    return true;
}

static int64 *
lrc_ctc_trellis_previous_state_cell(
    LrcCtcTrellis *trellis,
    int64 frame_index,
    int64 state_index
) {
    if (trellis == NULL) {
        return NULL;
    }
    if (trellis->previous_states == NULL) {
        return NULL;
    }
    if ((frame_index < 0) || (frame_index >= trellis->frame_count)) {
        return NULL;
    }
    if ((state_index < 0) || (state_index >= trellis->column_count)) {
        return NULL;
    }

    return trellis->previous_states
           + frame_index*trellis->column_count
           + state_index;
}

static float *
lrc_ctc_trellis_cell(
    LrcCtcTrellis *trellis,
    int64 frame_index,
    int64 state_index
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
    if ((state_index < 0) || (state_index >= trellis->column_count)) {
        return NULL;
    }

    return trellis->scores + frame_index*trellis->column_count + state_index;
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
    if ((cell_count > INT64_MAX/SIZEOF(*trellis->scores))
        || (cell_count > INT64_MAX/SIZEOF(*trellis->previous_states))) {
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
    trellis->previous_states = malloc2(
        cell_count*SIZEOF(*trellis->previous_states)
    );
    trellis->frame_count = frame_count;
    trellis->target_token_count = target_token_count;
    trellis->column_count = column_count;
    trellis->cell_count = cell_count;

    for (int64 i = 0; i < trellis->cell_count; i += 1) {
        trellis->scores[i] = -INFINITY;
        trellis->previous_states[i] = -1;
    }

    return true;
}

static bool
lrc_ctc_align_emissions_ready(
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
    if (!lrc_ctc_align_emissions_ready(emissions, result)) {
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

        *lrc_ctc_trellis_previous_state_cell(trellis, frame, 0) = 0;
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

static int32
lrc_ctc_align_graph_emission_token_id(
    LrcCtcAlignGraph *graph,
    int64 state_index,
    int32 blank_token_id
) {
    LrcCtcAlignState *state;

    ASSERT(lrc_ctc_align_graph_state_valid(graph, state_index));

    state = graph->states + state_index;
    if (state->kind == LRC_CTC_ALIGN_STATE_BLANK) {
        return blank_token_id;
    }

    ASSERT(state->kind == LRC_CTC_ALIGN_STATE_TOKEN);
    return state->token_id;
}

static void
lrc_ctc_trellis_try_candidate(
    LrcCtcTrellis *trellis,
    LrcCtcAlignGraph *graph,
    int64 frame,
    int64 state,
    int64 previous_state,
    float emission,
    float *best_score,
    int64 *best_previous_state
) {
    float *previous_cell;
    float candidate;

    ASSERT(trellis != NULL);
    ASSERT(graph != NULL);
    ASSERT(best_score != NULL);
    ASSERT(best_previous_state != NULL);

    if (!lrc_ctc_align_graph_transition_allowed(graph,
                                                 previous_state,
                                                 state)) {
        return;
    }

    previous_cell = lrc_ctc_trellis_cell(trellis, frame - 1, previous_state);
    ASSERT(previous_cell != NULL);
    if (!isfinite(*previous_cell)) {
        return;
    }

    candidate = *previous_cell + emission;
    if (candidate > *best_score) {
        *best_score = candidate;
        *best_previous_state = previous_state;
    }

    return;
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
    LrcCtcAlignGraph graph;
    int64 required_frame_count;
    float *cell;
    int64 *previous_state_cell;

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

    required_frame_count = lrc_ctc_required_frame_count_for_tokens(
        target_token_ids,
        target_token_count
    );
    if (required_frame_count <= 0) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_TOO_LARGE,
            "CTC required alignment frame count is invalid",
            -1,
            target_token_count
        );
        return false;
    }
    if (emissions->frame_count < required_frame_count) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_IMPOSSIBLE_ALIGNMENT,
            "CTC emissions have too few frames for target tokens",
            emissions->frame_count,
            target_token_count
        );
        return false;
    }

    lrc_ctc_align_graph_init(&graph);
    if (!lrc_ctc_align_graph_build(&graph,
                                   target_token_ids,
                                   target_token_count,
                                   result)) {
        return false;
    }
    if (!lrc_ctc_trellis_prepare(trellis,
                                 emissions,
                                 target_token_count,
                                 blank_token_id,
                                 result)) {
        lrc_ctc_align_graph_destroy(&graph);
        return false;
    }

    cell = lrc_ctc_trellis_cell(trellis, 0, 1);
    ASSERT(cell != NULL);
    *cell = lrc_ctc_emission_value(emissions, 0, target_token_ids[0]);

    for (int64 frame = 1; frame < trellis->frame_count; frame += 1) {
        for (int64 state = 1; state < trellis->column_count; state += 1) {
            float emission;
            float best_score;
            int64 best_previous_state;
            int32 token_id;

            token_id = lrc_ctc_align_graph_emission_token_id(&graph,
                                                             state,
                                                             blank_token_id);
            emission = lrc_ctc_emission_value(emissions, frame, token_id);
            best_score = -INFINITY;
            best_previous_state = -1;

            lrc_ctc_trellis_try_candidate(trellis,
                                          &graph,
                                          frame,
                                          state,
                                          state,
                                          emission,
                                          &best_score,
                                          &best_previous_state);
            lrc_ctc_trellis_try_candidate(trellis,
                                          &graph,
                                          frame,
                                          state,
                                          state - 1,
                                          emission,
                                          &best_score,
                                          &best_previous_state);
            lrc_ctc_trellis_try_candidate(trellis,
                                          &graph,
                                          frame,
                                          state,
                                          state - 2,
                                          emission,
                                          &best_score,
                                          &best_previous_state);

            cell = lrc_ctc_trellis_cell(trellis, frame, state);
            ASSERT(cell != NULL);
            *cell = best_score;

            previous_state_cell = lrc_ctc_trellis_previous_state_cell(
                trellis,
                frame,
                state
            );
            ASSERT(previous_state_cell != NULL);
            *previous_state_cell = best_previous_state;
        }
    }

    lrc_ctc_align_graph_destroy(&graph);

    return true;
}


static bool
lrc_ctc_trellis_ready_for_backtracking(
    LrcCtcTrellis *trellis,
    LrcCtcEmissions *emissions,
    int64 target_token_count,
    LrcCtcAlignResult *result
) {
    int64 state_count;

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
    if ((trellis->scores == NULL) || (trellis->previous_states == NULL)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_TRELLIS,
            "CTC trellis has not been scored",
            -1,
            -1
        );
        return false;
    }
    if (!lrc_ctc_align_graph_state_count(target_token_count,
                                          &state_count,
                                          result)) {
        return false;
    }
    if ((trellis->frame_count != emissions->frame_count)
        || (trellis->target_token_count != target_token_count)
        || (trellis->column_count != state_count)) {
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

static void
lrc_ctc_path_set_blank_step(
    LrcCtcPath *path,
    int64 frame_index,
    int64 state_index,
    int32 blank_token_id
) {
    ASSERT(path != NULL);
    ASSERT(path->steps != NULL);
    ASSERT(frame_index >= 0);
    ASSERT(frame_index < path->step_count);
    ASSERT(state_index >= 0);

    path->steps[frame_index].frame_index = frame_index;
    path->steps[frame_index].state_index = state_index;
    path->steps[frame_index].token_index = -1;
    path->steps[frame_index].token_id = blank_token_id;
    path->steps[frame_index].is_blank = true;

    return;
}

static void
lrc_ctc_path_set_token_step(
    LrcCtcPath *path,
    int64 frame_index,
    int64 state_index,
    int64 token_index,
    int32 token_id
) {
    ASSERT(path != NULL);
    ASSERT(path->steps != NULL);
    ASSERT(frame_index >= 0);
    ASSERT(frame_index < path->step_count);
    ASSERT(state_index >= 0);
    ASSERT(token_index >= 0);

    path->steps[frame_index].frame_index = frame_index;
    path->steps[frame_index].state_index = state_index;
    path->steps[frame_index].token_index = token_index;
    path->steps[frame_index].token_id = token_id;
    path->steps[frame_index].is_blank = false;

    return;
}

static void
lrc_ctc_path_set_graph_state_step(
    LrcCtcPath *path,
    LrcCtcAlignGraph *graph,
    int64 frame_index,
    int64 state_index,
    int32 blank_token_id
) {
    LrcCtcAlignState *state;

    ASSERT(path != NULL);
    ASSERT(path->steps != NULL);
    ASSERT(lrc_ctc_align_graph_state_valid(graph, state_index));
    ASSERT(frame_index >= 0);
    ASSERT(frame_index < path->step_count);

    state = graph->states + state_index;
    if (state->kind == LRC_CTC_ALIGN_STATE_BLANK) {
        lrc_ctc_path_set_blank_step(path,
                                    frame_index,
                                    state_index,
                                    blank_token_id);
        return;
    }

    ASSERT(state->kind == LRC_CTC_ALIGN_STATE_TOKEN);
    lrc_ctc_path_set_token_step(path,
                                frame_index,
                                state_index,
                                state->token_index,
                                state->token_id);

    return;
}

static bool
lrc_ctc_trellis_best_final_state(
    LrcCtcTrellis *trellis,
    int64 *final_state,
    LrcCtcAlignResult *result
) {
    int64 final_blank_state;
    int64 final_token_state;
    float *blank_cell;
    float *token_cell;

    ASSERT(trellis != NULL);
    ASSERT(trellis->scores != NULL);
    ASSERT(final_state != NULL);

    final_blank_state = trellis->column_count - 1;
    final_token_state = trellis->column_count - 2;

    blank_cell = lrc_ctc_trellis_cell(trellis,
                                      trellis->frame_count - 1,
                                      final_blank_state);
    token_cell = lrc_ctc_trellis_cell(trellis,
                                      trellis->frame_count - 1,
                                      final_token_state);
    ASSERT(blank_cell != NULL);
    ASSERT(token_cell != NULL);

    if (!isfinite(*blank_cell) && !isfinite(*token_cell)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_IMPOSSIBLE_ALIGNMENT,
            "CTC target tokens cannot fit in the available frames",
            trellis->frame_count - 1,
            trellis->target_token_count - 1
        );
        return false;
    }

    *final_state = final_token_state;
    if (*blank_cell > *token_cell) {
        *final_state = final_blank_state;
    }

    return true;
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
    LrcCtcAlignGraph graph;
    int64 state;
    int64 frame;

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

    lrc_ctc_align_graph_init(&graph);
    if (!lrc_ctc_align_graph_build(&graph,
                                   target_token_ids,
                                   target_token_count,
                                   result)) {
        lrc_ctc_path_destroy(path);
        return false;
    }
    if (!lrc_ctc_trellis_best_final_state(trellis, &state, result)) {
        lrc_ctc_align_graph_destroy(&graph);
        lrc_ctc_path_destroy(path);
        return false;
    }

    frame = trellis->frame_count - 1;
    while (true) {
        int64 *previous_state_cell;
        int64 previous_state;

        lrc_ctc_path_set_graph_state_step(path,
                                          &graph,
                                          frame,
                                          state,
                                          blank_token_id);
        if (frame == 0) {
            break;
        }

        previous_state_cell = lrc_ctc_trellis_previous_state_cell(trellis,
                                                                  frame,
                                                                  state);
        ASSERT(previous_state_cell != NULL);
        previous_state = *previous_state_cell;
        if (!lrc_ctc_align_graph_transition_allowed(&graph,
                                                     previous_state,
                                                     state)) {
            lrc_ctc_align_result_set(
                result,
                LRC_CTC_ALIGN_ERROR_INVALID_TRELLIS,
                "CTC trellis previous-state backpointer is invalid",
                frame,
                state
            );
            lrc_ctc_align_graph_destroy(&graph);
            lrc_ctc_path_destroy(path);
            return false;
        }

        state = previous_state;
        frame -= 1;
    }

    lrc_ctc_align_graph_destroy(&graph);

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
    if (step->state_index < 0) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_PATH,
            "CTC path state index is invalid",
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
    if (!lrc_ctc_align_emissions_ready(emissions, result)) {
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
    int64 previous_token_index;
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
    previous_token_index = -1;
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

            if (step->token_index != previous_token_index + 1) {
                lrc_ctc_align_result_set(
                    result,
                    LRC_CTC_ALIGN_ERROR_INVALID_PATH,
                    "CTC path token states are not target ordered",
                    step->frame_index,
                    step->token_index
                );
                lrc_ctc_token_spans_destroy(spans);
                return false;
            }

            span_index += 1;
            previous_token_index = step->token_index;
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
    int32 previous_end;

    ASSERT(word_count != NULL);

    *word_count = 0;
    in_word = false;
    previous_end = -1;
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
        if ((previous_end >= 0)
            && (previous_end < token->normalized_start)
            && lrc_ctc_normalized_range_has_space(normalized,
                                                  previous_end,
                                                  token->normalized_start)) {
            in_word = false;
        }
        if (lrc_ctc_normalized_range_is_space(normalized,
                                              token->normalized_start,
                                              token->normalized_end)) {
            in_word = false;
            previous_end = token->normalized_end;
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
        previous_end = token->normalized_end;
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
    int32 previous_end;
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
    previous_end = -1;
    for (int64 i = 0; i < token_spans->span_count; i += 1) {
        LrcCtcTokenSpan *token_span;
        LrcCtcTextToken *token;

        token_span = token_spans->spans + i;
        token = tokens->tokens + i;
        if ((previous_end >= 0)
            && (previous_end < token->normalized_start)
            && lrc_ctc_normalized_range_has_space(normalized,
                                                  previous_end,
                                                  token->normalized_start)) {
            word = NULL;
            score_count = 0;
            score_sum = 0.0f;
        }
        if (lrc_ctc_normalized_range_is_space(normalized,
                                              token->normalized_start,
                                              token->normalized_end)) {
            word = NULL;
            score_count = 0;
            score_sum = 0.0f;
            previous_end = token->normalized_end;
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
            previous_end = token->normalized_end;
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
        previous_end = token->normalized_end;
    }
    ASSERT(word_index + 1 == word_spans->span_count);

    return true;
}

static bool
lrc_ctc_line_timestamps_allocate(
    LrcCtcLineTimestamps *timestamps,
    int64 line_count,
    LrcCtcAlignResult *result
) {
    if (timestamps == NULL) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT,
            "CTC line timestamps destination is missing",
            -1,
            -1
        );
        return false;
    }
    if (line_count <= 0) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_WORD_SPANS,
            "CTC word spans did not produce lyric lines",
            -1,
            -1
        );
        return false;
    }
    if (line_count > INT64_MAX/SIZEOF(*timestamps->lines)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_TOO_LARGE,
            "CTC line timestamp allocation is too large",
            -1,
            line_count
        );
        return false;
    }

    lrc_ctc_line_timestamps_destroy(timestamps);
    timestamps->lines = malloc2(line_count*SIZEOF(*timestamps->lines));
    timestamps->line_count = line_count;
    timestamps->line_cap = line_count;
    timestamps->timestamped_line_count = 0;
    timestamps->blank_line_count = 0;

    for (int64 i = 0; i < timestamps->line_count; i += 1) {
        timestamps->lines[i].word_start_index = -1;
        timestamps->lines[i].word_end_index = -1;
        timestamps->lines[i].line_index = -1;
        timestamps->lines[i].start_seconds = 0.0f;
        timestamps->lines[i].end_seconds = 0.0f;
        timestamps->lines[i].score = -INFINITY;
        timestamps->lines[i].kind = LRC_CTC_LINE_TIMESTAMP_KIND_BLANK;
    }

    return true;
}

static bool
lrc_ctc_word_span_valid_for_lines(
    LrcCtcWordSpan *word,
    LrcLyricsNormalized *normalized,
    int64 word_index,
    LrcCtcAlignResult *result
) {
    int32 line_start;
    int32 line_end;

    if ((word->word_index != word_index)
        || (word->line_index < 0)
        || (word->line_index >= normalized->line_count)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_WORD_SPANS,
            "CTC word span has invalid indexes",
            -1,
            word_index
        );
        return false;
    }
    if (!lrc_lyrics_normalized_line_range(normalized,
                                          word->line_index,
                                          &line_start,
                                          &line_end)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_WORD_SPANS,
            "CTC word span does not belong to an alignable lyric line",
            -1,
            word_index
        );
        return false;
    }
    if ((word->normalized_start < line_start)
        || (word->normalized_end > line_end)
        || (word->normalized_end <= word->normalized_start)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_WORD_SPANS,
            "CTC word span normalized range is invalid",
            word->normalized_start,
            word_index
        );
        return false;
    }
    if (!isfinite(word->start_seconds) || !isfinite(word->end_seconds)
        || (word->end_seconds < word->start_seconds)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_WORD_SPANS,
            "CTC word span timing is invalid",
            -1,
            word_index
        );
        return false;
    }
    if (!isfinite(word->score)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_WORD_SPANS,
            "CTC word span score is invalid",
            -1,
            word_index
        );
        return false;
    }

    return true;
}

static bool
lrc_ctc_line_inputs_ready(
    LrcCtcWordSpans *word_spans,
    LrcLyricsNormalized *normalized,
    LrcCtcLineTimestamps *line_timestamps,
    LrcCtcAlignResult *result
) {
    int32 previous_line;
    float previous_start;

    if ((word_spans == NULL) || (normalized == NULL)
        || (line_timestamps == NULL)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT,
            "CTC line timestamp conversion received invalid arguments",
            -1,
            -1
        );
        return false;
    }
    if ((word_spans->spans == NULL) || (word_spans->span_count <= 0)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_WORD_SPANS,
            "CTC word spans are empty",
            -1,
            -1
        );
        return false;
    }
    if ((normalized->lines == NULL) || (normalized->line_count <= 0)) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_NORMALIZED_TEXT,
            "normalized lyric lines are not ready",
            -1,
            -1
        );
        return false;
    }

    previous_line = -1;
    previous_start = -INFINITY;
    for (int64 i = 0; i < word_spans->span_count; i += 1) {
        LrcCtcWordSpan *word;

        word = word_spans->spans + i;
        if (!lrc_ctc_word_span_valid_for_lines(word,
                                               normalized,
                                               i,
                                               result)) {
            return false;
        }
        if (word->line_index < previous_line) {
            lrc_ctc_align_result_set(
                result,
                LRC_CTC_ALIGN_ERROR_INVALID_WORD_SPANS,
                "CTC word spans must be ordered by lyric line",
                -1,
                i
            );
            return false;
        }
        if ((word->line_index == previous_line)
            && ((word->start_seconds + 0.00001f) < previous_start)) {
            lrc_ctc_align_result_set(
                result,
                LRC_CTC_ALIGN_ERROR_INVALID_WORD_SPANS,
                "CTC word spans must be time ordered inside each line",
                -1,
                i
            );
            return false;
        }

        previous_line = word->line_index;
        previous_start = word->start_seconds;
    }

    return true;
}

static bool
lrc_ctc_line_has_words(
    LrcCtcWordSpans *word_spans,
    int32 line_index,
    int64 *first_word_index,
    int64 *end_word_index
) {
    int64 first;
    int64 end;

    first = -1;
    end = -1;
    for (int64 i = 0; i < word_spans->span_count; i += 1) {
        if (word_spans->spans[i].line_index != line_index) {
            continue;
        }
        if (first < 0) {
            first = i;
        }
        end = i + 1;
    }

    if (first_word_index) {
        *first_word_index = first;
    }
    if (end_word_index) {
        *end_word_index = end;
    }

    return first >= 0;
}

static bool
lrc_ctc_count_line_timestamp_entries(
    LrcCtcWordSpans *word_spans,
    LrcLyricsNormalized *normalized,
    int64 *line_count,
    LrcCtcAlignResult *result
) {
    enum LrcLyricsNormalizedLineKind kind;

    *line_count = 0;
    for (int32 i = 0; i < normalized->line_count; i += 1) {
        kind = lrc_lyrics_normalized_line_kind(normalized, i);
        if (kind == LRC_LYRICS_NORMALIZED_LINE_KIND_BLANK) {
            *line_count += 1;
            continue;
        }
        if (kind == LRC_LYRICS_NORMALIZED_LINE_KIND_ALIGNABLE) {
            if (lrc_ctc_line_has_words(word_spans, i, NULL, NULL)) {
                *line_count += 1;
            }
            continue;
        }
    }
    if (*line_count <= 0) {
        lrc_ctc_align_result_set(
            result,
            LRC_CTC_ALIGN_ERROR_INVALID_WORD_SPANS,
            "CTC word spans did not map to lyric lines",
            -1,
            -1
        );
        return false;
    }

    return true;
}

static void
lrc_ctc_line_timestamp_set_blank(
    LrcCtcLineTimestamps *timestamps,
    int64 index,
    int32 line_index
) {
    LrcCtcLineTimestamp *line;

    ASSERT(timestamps != NULL);
    ASSERT(index >= 0);
    ASSERT(index < timestamps->line_count);

    line = timestamps->lines + index;
    line->word_start_index = -1;
    line->word_end_index = -1;
    line->line_index = line_index;
    line->start_seconds = 0.0f;
    line->end_seconds = 0.0f;
    line->score = -INFINITY;
    line->kind = LRC_CTC_LINE_TIMESTAMP_KIND_BLANK;
    timestamps->blank_line_count += 1;

    return;
}

static void
lrc_ctc_line_timestamp_set_timed(
    LrcCtcLineTimestamps *timestamps,
    int64 index,
    int32 line_index,
    LrcCtcWordSpans *word_spans,
    int64 first_word_index,
    int64 end_word_index
) {
    LrcCtcLineTimestamp *line;
    LrcCtcWordSpan *first;
    LrcCtcWordSpan *last;
    float score_sum;

    ASSERT(timestamps != NULL);
    ASSERT(index >= 0);
    ASSERT(index < timestamps->line_count);
    ASSERT(first_word_index >= 0);
    ASSERT(end_word_index > first_word_index);
    ASSERT(end_word_index <= word_spans->span_count);

    first = word_spans->spans + first_word_index;
    last = word_spans->spans + end_word_index - 1;
    score_sum = 0.0f;
    for (int64 i = first_word_index; i < end_word_index; i += 1) {
        score_sum += word_spans->spans[i].score;
    }

    line = timestamps->lines + index;
    line->word_start_index = first_word_index;
    line->word_end_index = end_word_index;
    line->line_index = line_index;
    line->start_seconds = first->start_seconds;
    line->end_seconds = last->end_seconds;
    line->score = score_sum/(float)(end_word_index - first_word_index);
    line->kind = LRC_CTC_LINE_TIMESTAMP_KIND_TIMESTAMPED;
    timestamps->timestamped_line_count += 1;

    return;
}

static bool
lrc_ctc_word_spans_to_line_timestamps(
    LrcCtcWordSpans *word_spans,
    LrcLyricsNormalized *normalized,
    LrcCtcLineTimestamps *line_timestamps,
    LrcCtcAlignResult *result
) {
    int64 line_count;
    int64 out_index;

    if (result) {
        lrc_ctc_align_result_init(result);
    }
    if (!lrc_ctc_line_inputs_ready(word_spans,
                                   normalized,
                                   line_timestamps,
                                   result)) {
        return false;
    }
    lrc_ctc_line_timestamps_destroy(line_timestamps);
    if (!lrc_ctc_count_line_timestamp_entries(word_spans,
                                              normalized,
                                              &line_count,
                                              result)) {
        return false;
    }
    if (!lrc_ctc_line_timestamps_allocate(line_timestamps,
                                          line_count,
                                          result)) {
        return false;
    }

    out_index = 0;
    for (int32 i = 0; i < normalized->line_count; i += 1) {
        enum LrcLyricsNormalizedLineKind kind;
        int64 first_word_index;
        int64 end_word_index;

        kind = lrc_lyrics_normalized_line_kind(normalized, i);
        if (kind == LRC_LYRICS_NORMALIZED_LINE_KIND_BLANK) {
            lrc_ctc_line_timestamp_set_blank(line_timestamps, out_index, i);
            out_index += 1;
            continue;
        }
        if (kind != LRC_LYRICS_NORMALIZED_LINE_KIND_ALIGNABLE) {
            continue;
        }
        if (!lrc_ctc_line_has_words(word_spans,
                                    i,
                                    &first_word_index,
                                    &end_word_index)) {
            continue;
        }

        lrc_ctc_line_timestamp_set_timed(line_timestamps,
                                         out_index,
                                         i,
                                         word_spans,
                                         first_word_index,
                                         end_word_index);
        out_index += 1;
    }
    ASSERT(out_index == line_timestamps->line_count);

    return true;
}


#if TESTING_ctc_align

#define CBASE_IMPLEMENT
#include "cbase.h"

#include "lyrics.c"
#include "ctc_tokenizer.c"
#include "audio.c"
#include "ctc_audio.c"
#include "ctc_model.c"
#include "ctc_inference.c"
#include "lrc.c"

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
ctc_align_load_alphabet_tokenizer_with_options(
    LrcCtcTokenizer *tokenizer,
    bool include_space
) {
    LrcCtcTokenizerResult result;
    StrBuilder builder;
    char temp_dir[PATH_MAX];
    char path[PATH_MAX];
    bool ok;

    sb_init(&builder);
    SB_APPEND(&builder, "<blank>\n");
    if (include_space) {
        SB_APPEND(&builder, "<space>\n");
    }
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
ctc_align_load_alphabet_tokenizer(LrcCtcTokenizer *tokenizer) {
    return ctc_align_load_alphabet_tokenizer_with_options(tokenizer, true);
}

static bool
ctc_align_load_no_space_alphabet_tokenizer(LrcCtcTokenizer *tokenizer) {
    return ctc_align_load_alphabet_tokenizer_with_options(tokenizer, false);
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

static bool
ctc_align_parse_lrc_file(
    LrcParsedFile *parsed,
    char *path,
    char **file_text,
    int32 *file_text_len
) {
    LrcParseResult result;

    ASSERT(parsed != NULL);
    ASSERT(file_text != NULL);
    ASSERT(file_text_len != NULL);

    *file_text = NULL;
    *file_text_len = 0;
    if ((path == NULL) || (path[0] == '\0') || !util_file_exists(path)) {
        return false;
    }

    *file_text = read_entire_file(path, file_text_len);
    if (!lrc_parse_text(parsed, *file_text, *file_text_len, &result)) {
        free2(*file_text, ((int64)*file_text_len + 1)*SIZEOF(**file_text));
        *file_text = NULL;
        *file_text_len = 0;
        return false;
    }

    return true;
}

static bool
ctc_align_expected_line_timestamp(
    LrcParsedFile *parsed,
    int32 source_line_index,
    float *timestamp_seconds
) {
    ASSERT(parsed != NULL);
    ASSERT(timestamp_seconds != NULL);

    for (int32 i = 0; i < parsed->line_count; i += 1) {
        LrcParsedLine *line = parsed->lines + i;

        if (line->kind != LRC_PARSED_LINE_KIND_TIMESTAMPED) {
            continue;
        }
        if (line->source_line_index != source_line_index) {
            continue;
        }

        *timestamp_seconds = line->timestamp_seconds;
        return true;
    }

    return false;
}

static int64
ctc_align_seconds_to_frame(float seconds, float frame_duration_seconds) {
    double frame;

    ASSERT(isfinite(seconds));
    ASSERT(seconds >= 0.0f);
    ASSERT(isfinite(frame_duration_seconds));
    ASSERT(frame_duration_seconds > 0.0f);

    frame = (double)seconds/(double)frame_duration_seconds + 0.5;
    if (frame > (double)INT64_MAX) {
        return -1;
    }

    return (int64)frame;
}

static bool
ctc_align_make_line_timed_token_frames(
    LrcParsedFile *expected,
    LrcCtcTokenizedText *tokens,
    float frame_duration_seconds,
    int64 *token_frames,
    int64 *frame_count
) {
    int32 current_line;
    int64 previous_frame;
    int64 line_start_frame;
    int64 line_token_offset;

    if ((expected == NULL) || (tokens == NULL) || (token_frames == NULL)
        || (frame_count == NULL) || (tokens->tokens == NULL)
        || (tokens->token_count <= 0)
        || !isfinite(frame_duration_seconds)
        || (frame_duration_seconds <= 0.0f)) {
        return false;
    }

    current_line = -1;
    previous_frame = -1;
    line_start_frame = -1;
    line_token_offset = 0;
    *frame_count = 0;
    for (int64 i = 0; i < tokens->token_count; i += 1) {
        LrcCtcTextToken *token = tokens->tokens + i;
        int64 frame;

        if (token->line_index != current_line) {
            float timestamp_seconds;

            if (!ctc_align_expected_line_timestamp(expected,
                                                   token->line_index,
                                                   &timestamp_seconds)) {
                return false;
            }
            line_start_frame = ctc_align_seconds_to_frame(
                timestamp_seconds,
                frame_duration_seconds
            );
            if (line_start_frame < 0) {
                return false;
            }
            if ((i > 0) && (line_start_frame > 0)) {
                line_start_frame -= 1;
            }
            if (line_start_frame <= previous_frame) {
                line_start_frame = previous_frame + 1;
            }

            current_line = token->line_index;
            line_token_offset = 0;
        }

        frame = line_start_frame + line_token_offset;
        if (frame <= previous_frame) {
            return false;
        }
        token_frames[i] = frame;
        previous_frame = frame;
        line_token_offset += 1;
    }

    if (previous_frame > INT64_MAX - 2) {
        return false;
    }
    *frame_count = previous_frame + 2;

    return true;
}

static void
ctc_align_fill_token_frame_values(
    float *values,
    int64 frame_count,
    int64 vocabulary_size,
    int32 blank_token_id,
    int32 *token_ids,
    int64 *token_frames,
    int64 token_count
) {
    for (int64 i = 0; i < frame_count*vocabulary_size; i += 1) {
        values[i] = -12.0f;
    }
    for (int64 frame = 0; frame < frame_count; frame += 1) {
        values[frame*vocabulary_size + blank_token_id] = -0.05f;
    }
    for (int64 i = 0; i < token_count; i += 1) {
        int64 frame = token_frames[i];

        ASSERT(frame >= 0);
        ASSERT(frame < frame_count);
        values[frame*vocabulary_size + blank_token_id] = -6.0f;
        values[frame*vocabulary_size + token_ids[i]] = -0.05f;
    }

    return;
}

static bool
ctc_align_parsed_files_close(
    LrcParsedFile *actual,
    LrcParsedFile *expected,
    float max_error_seconds
) {
    if ((actual == NULL) || (expected == NULL)) {
        return false;
    }
    if (actual->line_count != expected->line_count) {
        error2("LRC line count mismatch: actual=%d expected=%d\n",
               actual->line_count, expected->line_count);
        return false;
    }

    for (int32 i = 0; i < expected->line_count; i += 1) {
        LrcParsedLine *actual_line = actual->lines + i;
        LrcParsedLine *expected_line = expected->lines + i;
        float diff;

        if (actual_line->kind != expected_line->kind) {
            error2("LRC line %d kind mismatch\n", i);
            return false;
        }
        if (!strequal2(actual_line->text,
                       actual_line->text_len,
                       expected_line->text,
                       expected_line->text_len)) {
            error2("LRC line %d text mismatch\n", i);
            return false;
        }
        if (expected_line->kind != LRC_PARSED_LINE_KIND_TIMESTAMPED) {
            continue;
        }

        diff = fabsf(actual_line->timestamp_seconds
                     - expected_line->timestamp_seconds);
        if (diff > max_error_seconds) {
            error2(
                "LRC line %d timestamp diff %.3f actual %.3f expected %.3f\n",
                i,
                (double)diff,
                (double)actual_line->timestamp_seconds,
                (double)expected_line->timestamp_seconds
            );
            return false;
        }
    }

    return true;
}


static bool
ctc_align_output_lines_from_timestamps(
    LrcLyrics *lyrics,
    LrcCtcLineTimestamps *timestamps,
    LrcOutputLine *lines
) {
    if ((lyrics == NULL) || (timestamps == NULL) || (lines == NULL)) {
        return false;
    }
    if ((timestamps->line_count < 0)
        || (timestamps->line_count > INT32_MAX)) {
        return false;
    }

    for (int64 i = 0; i < timestamps->line_count; i += 1) {
        LrcCtcLineTimestamp *timestamp;
        LrcLyricsLine *lyrics_line;
        LrcFormatResult result;
        int32 hundredths;

        timestamp = timestamps->lines + i;
        if ((timestamp->line_index < 0)
            || (timestamp->line_index >= lyrics->line_count)) {
            return false;
        }

        lyrics_line = lyrics->lines + timestamp->line_index;
        lines[i].text = lyrics_line->text;
        lines[i].text_len = lyrics_line->text_len;
        lines[i].timestamp_hundredths = -1;

        switch (timestamp->kind) {
        case LRC_CTC_LINE_TIMESTAMP_KIND_TIMESTAMPED:
            if (!lrc_timestamp_hundredths_from_seconds(
                timestamp->start_seconds,
                &hundredths,
                &result
            )) {
                return false;
            }
            lines[i].kind = LRC_OUTPUT_LINE_KIND_TIMESTAMPED;
            lines[i].timestamp_hundredths = hundredths;
            break;
        case LRC_CTC_LINE_TIMESTAMP_KIND_BLANK:
            lines[i].kind = LRC_OUTPUT_LINE_KIND_BLANK;
            break;
        default:
            return false;
        }
    }

    return true;
}

static int32
ctc_align_test_empty_initializers(void) {
    LrcCtcAlignResult result;
    LrcCtcAlignGraph graph;
    LrcCtcTrellis trellis;
    LrcCtcPath path;
    LrcCtcTokenSpans spans;
    LrcCtcWordSpans word_spans;
    LrcCtcLineTimestamps line_timestamps;

    lrc_ctc_align_result_init(&result);
    lrc_ctc_align_graph_init(&graph);
    lrc_ctc_trellis_init(&trellis);
    lrc_ctc_path_init(&path);
    lrc_ctc_token_spans_init(&spans);
    lrc_ctc_word_spans_init(&word_spans);
    lrc_ctc_line_timestamps_init(&line_timestamps);

    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_NONE);
    ASSERT(strequal(result.message, "ok"));
    ASSERT(result.frame_index == -1);
    ASSERT(result.token_index == -1);

    ASSERT(graph.states == NULL);
    ASSERT(graph.state_count == 0);
    ASSERT(graph.target_token_count == 0);

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

    ASSERT(line_timestamps.lines == NULL);
    ASSERT(line_timestamps.line_count == 0);
    ASSERT(line_timestamps.line_cap == 0);
    ASSERT(line_timestamps.timestamped_line_count == 0);
    ASSERT(line_timestamps.blank_line_count == 0);

    return 0;
}

static int32
ctc_align_test_graph_build_layout(void) {
    LrcCtcAlignResult result;
    LrcCtcAlignGraph graph;
    int32 one_token[] = {7};
    int32 two_tokens[] = {4, 8};
    int32 repeated_tokens[] = {3, 3};

    lrc_ctc_align_graph_init(&graph);
    if (!lrc_ctc_align_graph_build(&graph, one_token, 1, &result)) {
        return ctc_align_test_fail("build one-token CTC graph");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_NONE);
    ASSERT(graph.target_token_count == 1);
    ASSERT(graph.state_count == 3);
    ASSERT(graph.states[0].kind == LRC_CTC_ALIGN_STATE_BLANK);
    ASSERT(graph.states[0].token_index == -1);
    ASSERT(graph.states[1].kind == LRC_CTC_ALIGN_STATE_TOKEN);
    ASSERT(graph.states[1].token_index == 0);
    ASSERT(graph.states[1].token_id == 7);
    ASSERT(graph.states[2].kind == LRC_CTC_ALIGN_STATE_BLANK);

    if (!lrc_ctc_align_graph_build(&graph, two_tokens, 2, &result)) {
        return ctc_align_test_fail("build two-token CTC graph");
    }
    ASSERT(graph.target_token_count == 2);
    ASSERT(graph.state_count == 5);
    ASSERT(graph.states[0].kind == LRC_CTC_ALIGN_STATE_BLANK);
    ASSERT(graph.states[1].kind == LRC_CTC_ALIGN_STATE_TOKEN);
    ASSERT(graph.states[1].token_index == 0);
    ASSERT(graph.states[1].token_id == 4);
    ASSERT(graph.states[2].kind == LRC_CTC_ALIGN_STATE_BLANK);
    ASSERT(graph.states[3].kind == LRC_CTC_ALIGN_STATE_TOKEN);
    ASSERT(graph.states[3].token_index == 1);
    ASSERT(graph.states[3].token_id == 8);
    ASSERT(graph.states[4].kind == LRC_CTC_ALIGN_STATE_BLANK);

    if (!lrc_ctc_align_graph_build(&graph, repeated_tokens, 2, &result)) {
        return ctc_align_test_fail("build repeated-token CTC graph");
    }
    ASSERT(graph.state_count == 5);
    ASSERT(graph.states[1].kind == LRC_CTC_ALIGN_STATE_TOKEN);
    ASSERT(graph.states[1].token_index == 0);
    ASSERT(graph.states[1].token_id == 3);
    ASSERT(graph.states[3].kind == LRC_CTC_ALIGN_STATE_TOKEN);
    ASSERT(graph.states[3].token_index == 1);
    ASSERT(graph.states[3].token_id == 3);

    lrc_ctc_align_graph_destroy(&graph);

    return 0;
}

static int32
ctc_align_test_graph_rejects_bad_inputs(void) {
    LrcCtcAlignResult result;
    LrcCtcAlignGraph graph;
    int32 tokens[] = {1};
    int64 state_count;

    lrc_ctc_align_graph_init(&graph);
    if (lrc_ctc_align_graph_build(NULL, tokens, 1, &result)) {
        return ctc_align_test_fail("missing graph accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT);

    if (lrc_ctc_align_graph_build(&graph, NULL, 1, &result)) {
        return ctc_align_test_fail("missing graph tokens accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT);

    if (lrc_ctc_align_graph_build(&graph, tokens, 0, &result)) {
        return ctc_align_test_fail("zero graph tokens accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_DIMENSIONS);

    if (lrc_ctc_align_graph_state_count(1, NULL, &result)) {
        return ctc_align_test_fail("missing state-count destination accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT);

    if (lrc_ctc_align_graph_state_count(INT64_MAX/2 + 1,
                                        &state_count,
                                        &result)) {
        return ctc_align_test_fail("huge graph state count accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_TOO_LARGE);
    ASSERT(state_count == 0);
    ASSERT(graph.states == NULL);

    return 0;
}

static int32
ctc_align_test_graph_transition_rules(void) {
    LrcCtcAlignResult result;
    LrcCtcAlignGraph graph;
    int32 different_tokens[] = {1, 2};
    int32 repeated_tokens[] = {1, 1};

    lrc_ctc_align_graph_init(&graph);
    if (!lrc_ctc_align_graph_build(&graph, different_tokens, 2, &result)) {
        return ctc_align_test_fail("build transition graph");
    }

    ASSERT(lrc_ctc_align_graph_transition_allowed(&graph, 0, 0));
    ASSERT(lrc_ctc_align_graph_transition_allowed(&graph, 0, 1));
    ASSERT(!lrc_ctc_align_graph_transition_allowed(&graph, 0, 2));
    ASSERT(lrc_ctc_align_state_can_skip(&graph, 1, 3));
    ASSERT(lrc_ctc_align_graph_transition_allowed(&graph, 1, 3));
    ASSERT(!lrc_ctc_align_graph_transition_allowed(&graph, 1, 4));
    ASSERT(!lrc_ctc_align_graph_transition_allowed(&graph, 3, 1));
    ASSERT(!lrc_ctc_align_graph_transition_allowed(&graph, -1, 0));
    ASSERT(!lrc_ctc_align_graph_transition_allowed(&graph, 0, 5));

    if (!lrc_ctc_align_graph_build(&graph, repeated_tokens, 2, &result)) {
        return ctc_align_test_fail("build repeated transition graph");
    }
    ASSERT(!lrc_ctc_align_state_can_skip(&graph, 1, 3));
    ASSERT(!lrc_ctc_align_graph_transition_allowed(&graph, 1, 3));
    ASSERT(lrc_ctc_align_graph_transition_allowed(&graph, 1, 2));
    ASSERT(lrc_ctc_align_graph_transition_allowed(&graph, 2, 3));

    lrc_ctc_align_graph_destroy(&graph);

    return 0;
}

static int32
ctc_align_test_required_frame_count_for_tokens(void) {
    int32 one_token[] = {1};
    int32 different_tokens[] = {1, 2};
    int32 repeated_tokens[] = {1, 1};
    int32 mixed_tokens[] = {1, 1, 2, 2};
    int32 separated_repeat_tokens[] = {1, 2, 1};

    ASSERT(lrc_ctc_required_frame_count_for_tokens(one_token, 1) == 1);
    ASSERT(lrc_ctc_required_frame_count_for_tokens(different_tokens, 2) == 2);
    ASSERT(lrc_ctc_required_frame_count_for_tokens(repeated_tokens, 2) == 3);
    ASSERT(lrc_ctc_required_frame_count_for_tokens(mixed_tokens, 4) == 6);
    ASSERT(lrc_ctc_required_frame_count_for_tokens(separated_repeat_tokens, 3)
           == 3);
    ASSERT(lrc_ctc_required_frame_count_for_tokens(NULL, 1) == -1);
    ASSERT(lrc_ctc_required_frame_count_for_tokens(one_token, 0) == -1);

    return 0;
}

static int32
ctc_align_test_score_rejects_too_few_repeated_frames(void) {
    LrcCtcAlignResult result;
    LrcCtcTrellis trellis;
    LrcCtcEmissions emissions;
    int32 target_token_ids[] = {1, 1};
    float values[] = {
        -0.10f, -0.20f,
        -0.20f, -0.10f,
    };

    ctc_align_make_emissions(&emissions, values, 2, 2);
    lrc_ctc_trellis_init(&trellis);
    if (lrc_ctc_trellis_score_forward(&trellis,
                                       &emissions,
                                       target_token_ids,
                                       2,
                                       0,
                                       &result)) {
        return ctc_align_test_fail("too-few repeated frames accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_IMPOSSIBLE_ALIGNMENT);
    ASSERT(result.frame_index == 2);
    ASSERT(result.token_index == 2);
    ASSERT(trellis.scores == NULL);

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
    ASSERT(trellis.column_count == 5);
    ASSERT(trellis.cell_count == 15);
    for (int64 i = 0; i < trellis.cell_count; i += 1) {
        ASSERT(ctc_align_is_negative_infinity(trellis.scores[i]));
    }
    ASSERT(lrc_ctc_trellis_cell(&trellis, 0, 0) == trellis.scores);
    ASSERT(lrc_ctc_trellis_cell(&trellis, 2, 4)
           == trellis.scores + 14);
    ASSERT(lrc_ctc_trellis_cell(&trellis, -1, 0) == NULL);
    ASSERT(lrc_ctc_trellis_cell(&trellis, 0, -1) == NULL);
    ASSERT(lrc_ctc_trellis_cell(&trellis, 3, 0) == NULL);
    ASSERT(lrc_ctc_trellis_cell(&trellis, 0, 5) == NULL);

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
    ASSERT(result.frame_index == -1);
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
    ASSERT(trellis.column_count == 5);
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
    ASSERT(trellis.column_count == 5);
    ASSERT(ctc_align_float_close(*lrc_ctc_trellis_cell(&trellis, 1, 1),
                                 -0.20f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(*lrc_ctc_trellis_cell(&trellis, 2, 3),
                                 -0.30f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(*lrc_ctc_trellis_cell(&trellis, 3, 4),
                                 -0.40f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(*lrc_ctc_trellis_cell(&trellis, 3, 0),
                                 -10.20f,
                                 0.00001f));
    ASSERT(*lrc_ctc_trellis_previous_state_cell(&trellis, 2, 3) == 1);
    ASSERT(*lrc_ctc_trellis_previous_state_cell(&trellis, 3, 4) == 3);

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
    ASSERT(trellis.column_count == 3);
    ASSERT(ctc_align_float_close(*lrc_ctc_trellis_cell(&trellis, 1, 1),
                                 -0.30f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(*lrc_ctc_trellis_cell(&trellis, 2, 2),
                                 -0.40f,
                                 0.00001f));
    ASSERT(*lrc_ctc_trellis_previous_state_cell(&trellis, 2, 2) == 1);

    lrc_ctc_trellis_destroy(&trellis);

    return 0;
}

static int32
ctc_align_test_trellis_uses_graph_state_columns(void) {
    LrcCtcAlignResult result;
    LrcCtcTrellis trellis;

    lrc_ctc_trellis_init(&trellis);
    if (!lrc_ctc_trellis_allocate(&trellis, 2, 1, &result)) {
        return ctc_align_test_fail("allocate one-token state trellis");
    }
    ASSERT(trellis.column_count == 3);
    ASSERT(trellis.cell_count == 6);
    lrc_ctc_trellis_destroy(&trellis);

    if (!lrc_ctc_trellis_allocate(&trellis, 2, 2, &result)) {
        return ctc_align_test_fail("allocate two-token state trellis");
    }
    ASSERT(trellis.column_count == 5);
    ASSERT(trellis.cell_count == 10);
    lrc_ctc_trellis_destroy(&trellis);

    if (!lrc_ctc_trellis_allocate(&trellis, 2, 3, &result)) {
        return ctc_align_test_fail("allocate three-token state trellis");
    }
    ASSERT(trellis.column_count == 7);
    ASSERT(trellis.cell_count == 14);
    lrc_ctc_trellis_destroy(&trellis);

    return 0;
}

static int32
ctc_align_test_forward_scores_ctc_skip_transition(void) {
    LrcCtcAlignResult result;
    LrcCtcTrellis trellis;
    LrcCtcEmissions emissions;
    int32 target_token_ids[] = {1, 2};
    float values[] = {
        -5.00f, -0.10f, -5.00f,
        -5.00f, -5.00f, -0.20f,
    };

    ctc_align_make_emissions(&emissions, values, 2, 3);
    lrc_ctc_trellis_init(&trellis);
    if (!lrc_ctc_trellis_score_forward(&trellis,
                                        &emissions,
                                        target_token_ids,
                                        2,
                                        0,
                                        &result)) {
        return ctc_align_test_fail("score CTC skip transition");
    }

    ASSERT(ctc_align_float_close(*lrc_ctc_trellis_cell(&trellis, 1, 3),
                                 -0.30f,
                                 0.00001f));
    ASSERT(*lrc_ctc_trellis_previous_state_cell(&trellis, 1, 3) == 1);

    lrc_ctc_trellis_destroy(&trellis);

    return 0;
}

static int32
ctc_align_test_best_final_state_selection(void) {
    LrcCtcAlignResult result;
    LrcCtcTrellis trellis;
    LrcCtcEmissions emissions;
    int64 final_state;
    int32 target_token_ids[] = {1};
    float token_values[] = {
        -5.00f, -0.10f,
    };
    float blank_values[] = {
        -5.00f, -0.10f,
        -0.20f, -5.00f,
    };

    ctc_align_make_emissions(&emissions, token_values, 1, 2);
    lrc_ctc_trellis_init(&trellis);
    if (!lrc_ctc_trellis_score_forward(&trellis,
                                        &emissions,
                                        target_token_ids,
                                        1,
                                        0,
                                        &result)) {
        return ctc_align_test_fail("score final token state");
    }
    if (!lrc_ctc_trellis_best_final_state(&trellis, &final_state, &result)) {
        return ctc_align_test_fail("select final token state");
    }
    ASSERT(final_state == 1);
    lrc_ctc_trellis_destroy(&trellis);

    ctc_align_make_emissions(&emissions, blank_values, 2, 2);
    if (!lrc_ctc_trellis_score_forward(&trellis,
                                        &emissions,
                                        target_token_ids,
                                        1,
                                        0,
                                        &result)) {
        return ctc_align_test_fail("score final blank state");
    }
    if (!lrc_ctc_trellis_best_final_state(&trellis, &final_state, &result)) {
        return ctc_align_test_fail("select final blank state");
    }
    ASSERT(final_state == 2);
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
    ASSERT(path.steps[0].state_index == 0);
    ASSERT(path.steps[0].is_blank);
    ASSERT(path.steps[0].token_id == 0);
    ASSERT(path.steps[1].frame_index == 1);
    ASSERT(path.steps[1].state_index == 1);
    ASSERT(!path.steps[1].is_blank);
    ASSERT(path.steps[1].token_index == 0);
    ASSERT(path.steps[1].token_id == 1);
    ASSERT(path.steps[2].frame_index == 2);
    ASSERT(path.steps[2].state_index == 3);
    ASSERT(!path.steps[2].is_blank);
    ASSERT(path.steps[2].token_index == 1);
    ASSERT(path.steps[2].token_id == 2);
    ASSERT(path.steps[3].frame_index == 3);
    ASSERT(path.steps[3].state_index == 4);
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
        -0.10f, -5.00f,
        -5.00f, -0.20f,
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
    ASSERT(path.steps[1].state_index == 1);
    ASSERT(!path.steps[1].is_blank);
    ASSERT(path.steps[1].token_index == 0);
    ASSERT(path.steps[1].token_id == 1);
    ASSERT(path.steps[2].frame_index == 2);
    ASSERT(path.steps[2].state_index == 2);
    ASSERT(path.steps[2].is_blank);
    ASSERT(path.steps[2].token_id == 0);
    ASSERT(path.steps[3].frame_index == 3);
    ASSERT(path.steps[3].state_index == 3);
    ASSERT(!path.steps[3].is_blank);
    ASSERT(path.steps[3].token_index == 1);
    ASSERT(path.steps[3].token_id == 1);

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
    *lrc_ctc_trellis_cell(&trellis, trellis.frame_count - 1, 3) = -INFINITY;
    *lrc_ctc_trellis_cell(&trellis, trellis.frame_count - 1, 4) = -INFINITY;
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
        -0.10f, -5.00f,
        -5.00f, -0.20f,
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
    ASSERT(spans.spans[1].start_frame == 3);
    ASSERT(ctc_align_float_close(spans.spans[0].start_seconds,
                                 0.25f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(spans.spans[1].start_seconds,
                                 0.75f,
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

    lrc_ctc_path_set_token_step(&path, 0, 1, 0, 1);
    lrc_ctc_path_set_token_step(&path, 1, 1, 0, 1);
    lrc_ctc_path_set_blank_step(&path, 2, 2, 0);
    lrc_ctc_path_set_token_step(&path, 3, 3, 1, 2);
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
    lrc_ctc_path_set_token_step(&path, 1, 1, 0, 1);
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
ctc_align_test_token_spans_reject_out_of_order_targets(void) {
    LrcCtcAlignResult result;
    LrcCtcPath path;
    LrcCtcEmissions emissions;
    LrcCtcTokenSpans spans;
    float values[] = {
        -5.00f, -0.10f,
    };

    ctc_align_make_emissions(&emissions, values, 1, 2);
    lrc_ctc_path_init(&path);
    lrc_ctc_token_spans_init(&spans);
    if (!lrc_ctc_path_allocate(&path, 1, &result)) {
        return ctc_align_test_fail("allocate out-of-order span path");
    }

    lrc_ctc_path_set_token_step(&path, 0, 3, 1, 1);
    if (lrc_ctc_path_to_token_spans(&path,
                                    &emissions,
                                    0.1f,
                                    &spans,
                                    &result)) {
        return ctc_align_test_fail("out-of-order path target accepted");
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
ctc_align_test_word_spans_use_skipped_space_gaps(void) {
    LrcLyrics lyrics;
    LrcLyricsNormalized normalized;
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizedText tokens;
    LrcCtcTokenSpans token_spans;
    LrcCtcWordSpans word_spans;
    LrcCtcTokenizeResult tokenize_result;
    LrcCtcAlignResult result;
    char text[] = "Hi Bob\n";

    lrc_lyrics_normalized_init(&normalized);
    lrc_ctc_tokenizer_init(&tokenizer);
    lrc_ctc_tokenized_text_init(&tokens);
    lrc_ctc_token_spans_init(&token_spans);
    lrc_ctc_word_spans_init(&word_spans);
    if (!ctc_align_load_lyrics_text(&lyrics, text, strlen32(text))) {
        return ctc_align_test_fail("load skipped-space lyrics");
    }
    if (!lrc_lyrics_normalize(&lyrics, &normalized)) {
        return ctc_align_test_fail("normalize skipped-space lyrics");
    }
    if (!ctc_align_load_no_space_alphabet_tokenizer(&tokenizer)) {
        return ctc_align_test_fail("load no-space word tokenizer");
    }
    if (!lrc_ctc_tokenizer_tokenize_normalized(&tokenizer,
                                               &normalized,
                                               &tokens,
                                               &tokenize_result)) {
        return ctc_align_test_fail("tokenize skipped-space lyrics");
    }
    if (!ctc_align_make_token_spans_from_tokens(&tokens,
                                                0.0f,
                                                0.10f,
                                                &token_spans)) {
        return ctc_align_test_fail("make skipped-space token spans");
    }
    if (!lrc_ctc_token_spans_to_word_spans(&token_spans,
                                           &tokens,
                                           &normalized,
                                           &word_spans,
                                           &result)) {
        return ctc_align_test_fail("convert skipped-space word spans");
    }

    ASSERT(strequal2(normalized.text, normalized.text_len, "hi bob", 6));
    ASSERT(tokens.token_count == 5);
    ASSERT(tokens.tokens[0].normalized_start == 0);
    ASSERT(tokens.tokens[1].normalized_start == 1);
    ASSERT(tokens.tokens[2].normalized_start == 3);
    ASSERT(word_spans.span_count == 2);
    ctc_align_assert_word_text(&normalized,
                               word_spans.spans + 0,
                               STRLIT("hi"));
    ctc_align_assert_word_text(&normalized,
                               word_spans.spans + 1,
                               STRLIT("bob"));
    ASSERT(word_spans.spans[0].token_start_index == 0);
    ASSERT(word_spans.spans[0].token_end_index == 2);
    ASSERT(word_spans.spans[1].token_start_index == 2);
    ASSERT(word_spans.spans[1].token_end_index == 5);
    ASSERT(ctc_align_float_close(word_spans.spans[1].start_seconds,
                                 0.2f,
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
ctc_align_test_line_timestamps_from_generated_words(void) {
    LrcLyrics lyrics;
    LrcLyricsNormalized normalized;
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizedText tokens;
    LrcCtcTokenSpans token_spans;
    LrcCtcWordSpans word_spans;
    LrcCtcLineTimestamps line_timestamps;
    LrcCtcAlignResult result;
    char text[] = "Alpha beta\n\nGamma!\n!!!\nDelta\n";

    lrc_ctc_token_spans_init(&token_spans);
    lrc_ctc_word_spans_init(&word_spans);
    lrc_ctc_line_timestamps_init(&line_timestamps);
    if (!ctc_align_load_tokenized_lyrics(text,
                                         strlen32(text),
                                         &lyrics,
                                         &normalized,
                                         &tokenizer,
                                         &tokens)) {
        return ctc_align_test_fail("load generated line lyrics");
    }
    if (!ctc_align_make_token_spans_from_tokens(&tokens,
                                                0.0f,
                                                0.10f,
                                                &token_spans)) {
        return ctc_align_test_fail("make generated line token spans");
    }
    if (!lrc_ctc_token_spans_to_word_spans(&token_spans,
                                           &tokens,
                                           &normalized,
                                           &word_spans,
                                           &result)) {
        return ctc_align_test_fail("convert generated line word spans");
    }
    if (!lrc_ctc_word_spans_to_line_timestamps(&word_spans,
                                               &normalized,
                                               &line_timestamps,
                                               &result)) {
        return ctc_align_test_fail("convert generated line timestamps");
    }

    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_NONE);
    ASSERT(line_timestamps.line_count == 4);
    ASSERT(line_timestamps.timestamped_line_count == 3);
    ASSERT(line_timestamps.blank_line_count == 1);

    ASSERT(line_timestamps.lines[0].kind
           == LRC_CTC_LINE_TIMESTAMP_KIND_TIMESTAMPED);
    ASSERT(line_timestamps.lines[0].line_index == 0);
    ASSERT(line_timestamps.lines[0].word_start_index == 0);
    ASSERT(line_timestamps.lines[0].word_end_index == 2);
    ASSERT(ctc_align_float_close(line_timestamps.lines[0].start_seconds,
                                 0.0f,
                                 0.00001f));
    ASSERT(ctc_align_float_close(line_timestamps.lines[0].end_seconds,
                                 1.0f,
                                 0.00001f));

    ASSERT(line_timestamps.lines[1].kind
           == LRC_CTC_LINE_TIMESTAMP_KIND_BLANK);
    ASSERT(line_timestamps.lines[1].line_index == 1);
    ASSERT(line_timestamps.lines[1].word_start_index == -1);
    ASSERT(line_timestamps.lines[1].word_end_index == -1);

    ASSERT(line_timestamps.lines[2].kind
           == LRC_CTC_LINE_TIMESTAMP_KIND_TIMESTAMPED);
    ASSERT(line_timestamps.lines[2].line_index == 2);
    ASSERT(line_timestamps.lines[2].word_start_index == 2);
    ASSERT(line_timestamps.lines[2].word_end_index == 3);
    ASSERT(ctc_align_float_close(line_timestamps.lines[2].start_seconds,
                                 1.1f,
                                 0.00001f));

    ASSERT(line_timestamps.lines[3].kind
           == LRC_CTC_LINE_TIMESTAMP_KIND_TIMESTAMPED);
    ASSERT(line_timestamps.lines[3].line_index == 4);
    ASSERT(line_timestamps.lines[3].word_start_index == 3);
    ASSERT(line_timestamps.lines[3].word_end_index == 4);
    ASSERT(ctc_align_float_close(line_timestamps.lines[3].start_seconds,
                                 1.7f,
                                 0.00001f));

    lrc_ctc_line_timestamps_destroy(&line_timestamps);
    lrc_ctc_word_spans_destroy(&word_spans);
    lrc_ctc_token_spans_destroy(&token_spans);
    lrc_ctc_tokenized_text_destroy(&tokens);
    lrc_ctc_tokenizer_destroy(&tokenizer);
    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);

    return 0;
}

static int32
ctc_align_test_line_timestamps_reject_bad_inputs(void) {
    LrcLyrics lyrics;
    LrcLyricsNormalized normalized;
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizedText tokens;
    LrcCtcTokenSpans token_spans;
    LrcCtcWordSpans word_spans;
    LrcCtcLineTimestamps line_timestamps;
    LrcCtcAlignResult result;
    char text[] = "a b\n";

    lrc_ctc_line_timestamps_init(&line_timestamps);
    if (lrc_ctc_word_spans_to_line_timestamps(NULL,
                                              &normalized,
                                              &line_timestamps,
                                              &result)) {
        return ctc_align_test_fail("missing word spans accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_ARGUMENT);

    lrc_ctc_token_spans_init(&token_spans);
    lrc_ctc_word_spans_init(&word_spans);
    if (!ctc_align_load_tokenized_lyrics(text,
                                         strlen32(text),
                                         &lyrics,
                                         &normalized,
                                         &tokenizer,
                                         &tokens)) {
        return ctc_align_test_fail("load bad line lyrics");
    }
    if (!ctc_align_make_token_spans_from_tokens(&tokens,
                                                0.0f,
                                                0.10f,
                                                &token_spans)) {
        return ctc_align_test_fail("make bad line token spans");
    }
    if (!lrc_ctc_token_spans_to_word_spans(&token_spans,
                                           &tokens,
                                           &normalized,
                                           &word_spans,
                                           &result)) {
        return ctc_align_test_fail("convert bad line word spans");
    }

    word_spans.spans[0].line_index = -1;
    if (lrc_ctc_word_spans_to_line_timestamps(&word_spans,
                                              &normalized,
                                              &line_timestamps,
                                              &result)) {
        return ctc_align_test_fail("bad word line accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_WORD_SPANS);
    word_spans.spans[0].line_index = 0;

    word_spans.spans[1].line_index = 0;
    word_spans.spans[1].start_seconds = -INFINITY;
    if (lrc_ctc_word_spans_to_line_timestamps(&word_spans,
                                              &normalized,
                                              &line_timestamps,
                                              &result)) {
        return ctc_align_test_fail("bad word timing accepted");
    }
    ASSERT(result.error == LRC_CTC_ALIGN_ERROR_INVALID_WORD_SPANS);

    lrc_ctc_line_timestamps_destroy(&line_timestamps);
    lrc_ctc_word_spans_destroy(&word_spans);
    lrc_ctc_token_spans_destroy(&token_spans);
    lrc_ctc_tokenized_text_destroy(&tokens);
    lrc_ctc_tokenizer_destroy(&tokenizer);
    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);

    return 0;
}

static int32
ctc_align_test_maxwell_line_timestamp_comparison(void) {
    LrcLyrics lyrics;
    LrcLyricsNormalized normalized;
    LrcLyricsLoadResult lyrics_result;
    LrcParsedFile parsed;
    LrcParseResult parse_result;
    LrcCtcWordSpans word_spans;
    LrcCtcLineTimestamps line_timestamps;
    LrcCtcAlignResult align_result;
    char *lyrics_path;
    char *lrc_path;
    char *lrc_text;
    int32 lrc_text_len;
    int64 word_index;

    lyrics_path = getenv("LRC_TEST_MAXWELL_TXT");
    if (lyrics_path == NULL) {
        lyrics_path = "next-phase/maxwell.txt";
    }
    lrc_path = getenv("LRC_TEST_MAXWELL_LRC");
    if (lrc_path == NULL) {
        lrc_path = "next-phase/maxwell.lrc";
    }
    if (!util_file_exists(lyrics_path) || !util_file_exists(lrc_path)) {
        return 0;
    }

    lrc_lyrics_init(&lyrics);
    lrc_lyrics_normalized_init(&normalized);
    lrc_parsed_file_init(&parsed);
    lrc_ctc_word_spans_init(&word_spans);
    lrc_ctc_line_timestamps_init(&line_timestamps);
    if (!lrc_lyrics_load_file(&lyrics, lyrics_path, &lyrics_result)) {
        return ctc_align_test_fail("load maxwell line lyrics");
    }
    if (!lrc_lyrics_normalize(&lyrics, &normalized)) {
        return ctc_align_test_fail("normalize maxwell line lyrics");
    }

    lrc_text = read_entire_file(lrc_path, &lrc_text_len);
    if (!lrc_parse_text(&parsed, lrc_text, lrc_text_len, &parse_result)) {
        free2(lrc_text, ((int64)lrc_text_len + 1)*SIZEOF(*lrc_text));
        return ctc_align_test_fail("parse maxwell expected lrc");
    }
    if (!lrc_ctc_word_spans_allocate(&word_spans,
                                     parsed.timestamped_line_count,
                                     &align_result)) {
        free2(lrc_text, ((int64)lrc_text_len + 1)*SIZEOF(*lrc_text));
        return ctc_align_test_fail("allocate maxwell expected words");
    }

    word_index = 0;
    for (int32 i = 0; i < parsed.line_count; i += 1) {
        LrcParsedLine *line;
        LrcCtcWordSpan *word;
        int32 line_start;
        int32 line_end;

        line = parsed.lines + i;
        if (line->kind != LRC_PARSED_LINE_KIND_TIMESTAMPED) {
            continue;
        }
        ASSERT(lrc_lyrics_normalized_line_range(&normalized,
                                                line->source_line_index,
                                                &line_start,
                                                &line_end));
        word = word_spans.spans + word_index;
        word->word_index = word_index;
        word->token_start_index = word_index;
        word->token_end_index = word_index + 1;
        word->span_start_index = word_index;
        word->span_end_index = word_index + 1;
        word->normalized_start = line_start;
        word->normalized_end = line_end;
        word->line_index = line->source_line_index;
        word->start_seconds = line->timestamp_seconds;
        word->end_seconds = line->timestamp_seconds + 0.5f;
        word->score = -0.10f;
        word_index += 1;
    }
    ASSERT(word_index == word_spans.span_count);

    if (!lrc_ctc_word_spans_to_line_timestamps(&word_spans,
                                               &normalized,
                                               &line_timestamps,
                                               &align_result)) {
        free2(lrc_text, ((int64)lrc_text_len + 1)*SIZEOF(*lrc_text));
        return ctc_align_test_fail("convert maxwell line timestamps");
    }

    ASSERT(line_timestamps.line_count == parsed.line_count);
    ASSERT(line_timestamps.timestamped_line_count
           == parsed.timestamped_line_count);
    ASSERT(line_timestamps.blank_line_count == parsed.blank_line_count);
    for (int32 i = 0; i < parsed.line_count; i += 1) {
        LrcParsedLine *expected;
        LrcCtcLineTimestamp *actual;

        expected = parsed.lines + i;
        actual = line_timestamps.lines + i;
        ASSERT(actual->line_index == expected->source_line_index);
        if (expected->kind == LRC_PARSED_LINE_KIND_BLANK) {
            ASSERT(actual->kind == LRC_CTC_LINE_TIMESTAMP_KIND_BLANK);
            continue;
        }

        ASSERT(actual->kind == LRC_CTC_LINE_TIMESTAMP_KIND_TIMESTAMPED);
        ASSERT(ctc_align_float_close(actual->start_seconds,
                                     expected->timestamp_seconds,
                                     0.015f));
    }

    lrc_ctc_line_timestamps_destroy(&line_timestamps);
    lrc_ctc_word_spans_destroy(&word_spans);
    lrc_parsed_file_destroy(&parsed);
    free2(lrc_text, ((int64)lrc_text_len + 1)*SIZEOF(*lrc_text));
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
ctc_align_test_full_synthetic_lrc_pipeline(void) {
    AudioTestSineOptions sine_options;
    LrcCtcAudioConfig audio_config;
    LrcCtcAudioResult audio_result;
    LrcCtcAudio audio;
    LrcCtcModelConfig model_config;
    LrcCtcModelInputResult model_result;
    LrcCtcModelInput input;
    LrcLyrics lyrics;
    LrcLyricsLoadResult lyrics_result;
    LrcLyricsNormalized normalized;
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizedText tokens;
    LrcCtcTokenizeResult tokenize_result;
    LrcCtcFakeInference fake;
    LrcCtcInferenceBackend backend;
    LrcCtcInferenceResult inference_result;
    LrcCtcEmissions emissions;
    LrcCtcAlignResult align_result;
    LrcCtcTrellis trellis;
    LrcCtcPath path;
    LrcCtcTokenSpans token_spans;
    LrcCtcWordSpans word_spans;
    LrcCtcLineTimestamps line_timestamps;
    LrcOutputLine *output_lines;
    LrcWriteResult write_result;
    char temp_dir[PATH_MAX];
    char lyrics_path[PATH_MAX];
    char wav_path[PATH_MAX];
    char lrc_path[PATH_MAX];
    char lyrics_text[] = "ab cd\n\nef\n";
    char expected_lrc[] = "[00:00.01]ab cd\n\n[00:00.07]ef\n";
    char *written_lrc;
    int32 written_lrc_len;
    int32 *target_token_ids;
    float *values;
    float frame_duration_seconds;
    int64 frame_count;
    int64 vocabulary_size;
    int64 value_count;
    int64 token_count;
    bool ok;

    if (!test_command_exists("ffmpeg")) {
        return 0;
    }

    lrc_ctc_audio_init(&audio);
    lrc_ctc_model_input_init(&input);
    lrc_lyrics_init(&lyrics);
    lrc_lyrics_normalized_init(&normalized);
    lrc_ctc_tokenizer_init(&tokenizer);
    lrc_ctc_tokenized_text_init(&tokens);
    lrc_ctc_fake_inference_init(&fake);
    lrc_ctc_emissions_init(&emissions);
    lrc_ctc_trellis_init(&trellis);
    lrc_ctc_path_init(&path);
    lrc_ctc_token_spans_init(&token_spans);
    lrc_ctc_word_spans_init(&word_spans);
    lrc_ctc_line_timestamps_init(&line_timestamps);

    target_token_ids = NULL;
    values = NULL;
    output_lines = NULL;
    written_lrc = NULL;
    written_lrc_len = 0;
    ok = true;

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "ctc_full_lrc");
    ctc_align_join_path(lyrics_path, SIZEOF(lyrics_path), temp_dir,
                        "lyrics.txt");
    ctc_align_join_path(wav_path, SIZEOF(wav_path), temp_dir, "song.wav");
    ctc_align_join_path(lrc_path, SIZEOF(lrc_path), temp_dir, "out.lrc");
    write_entire_file(lyrics_path, lyrics_text, strlen32(lyrics_text));

    audio_test_sine_options_init(&sine_options);
    sine_options.format.sample_rate = 16000;
    sine_options.format.channel_count = 2;
    sine_options.duration_seconds = 0.05;
    sine_options.frequency_hz = 330.0;
    if (!audio_test_generate_sine_wav(wav_path, &sine_options, "ffmpeg")) {
        ok = false;
    }

    lrc_ctc_audio_config_init(&audio_config);
    audio_config.sample_rate = 16000;
    if (ok && !lrc_ctc_audio_decode_file(&audio,
                                         wav_path,
                                         &audio_config,
                                         &audio_result)) {
        ok = false;
    }
    if (ok && (audio.channel_count != 1)) {
        ok = false;
    }
    if (ok && (audio.sample_rate != 16000)) {
        ok = false;
    }
    if (ok && (audio.sample_count <= 0)) {
        ok = false;
    }

    lrc_ctc_model_config_init(&model_config);
    model_config.sample_rate = 16000;
    model_config.inputs_to_logits_ratio = 160;
    model_config.window_seconds = 1;
    model_config.context_seconds = 0;
    if (ok && !lrc_ctc_model_input_prepare(&input,
                                           &audio,
                                           &model_config,
                                           &model_result)) {
        ok = false;
    }
    if (ok && ((input.shape_len != 2) || (input.shape[0] != 1)
               || (input.shape[1] != audio.sample_count))) {
        ok = false;
    }

    if (ok && !lrc_lyrics_load_file(&lyrics, lyrics_path, &lyrics_result)) {
        ok = false;
    }
    if (ok && !lrc_lyrics_normalize(&lyrics, &normalized)) {
        ok = false;
    }
    if (ok && !ctc_align_load_alphabet_tokenizer(&tokenizer)) {
        ok = false;
    }
    if (ok && !lrc_ctc_tokenizer_tokenize_normalized(&tokenizer,
                                                     &normalized,
                                                     &tokens,
                                                     &tokenize_result)) {
        ok = false;
    }

    if (ok && (tokens.token_count <= 0)) {
        ok = false;
    }
    token_count = tokens.token_count;
    frame_count = token_count + 2;
    vocabulary_size = tokenizer.token_count;
    value_count = frame_count*vocabulary_size;
    if (ok) {
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
    }

    if (ok && !lrc_ctc_fake_inference_set(&fake,
                                          values,
                                          frame_count,
                                          vocabulary_size)) {
        ok = false;
    }
    if (ok) {
        lrc_ctc_fake_inference_backend(&fake, &backend);
        if (!lrc_ctc_inference_run(&backend,
                                   &input,
                                   &emissions,
                                   &inference_result)) {
            ok = false;
        }
    }
    if (ok && !lrc_ctc_emissions_convert_to_log_probabilities(
        &emissions,
        LRC_CTC_EMISSION_VALUES_LOG_PROBABILITIES,
        &inference_result
    )) {
        ok = false;
    }

    if (ok && !lrc_ctc_trellis_score_forward(&trellis,
                                             &emissions,
                                             target_token_ids,
                                             token_count,
                                             tokenizer.blank_id,
                                             &align_result)) {
        ok = false;
    }
    if (ok && !lrc_ctc_trellis_backtrack(&trellis,
                                         &emissions,
                                         target_token_ids,
                                         token_count,
                                         tokenizer.blank_id,
                                         &path,
                                         &align_result)) {
        ok = false;
    }

    frame_duration_seconds = (float)(input.stride_ms/1000.0);
    if (ok && !lrc_ctc_path_to_token_spans(&path,
                                           &emissions,
                                           frame_duration_seconds,
                                           &token_spans,
                                           &align_result)) {
        ok = false;
    }
    if (ok && !lrc_ctc_token_spans_to_word_spans(&token_spans,
                                                 &tokens,
                                                 &normalized,
                                                 &word_spans,
                                                 &align_result)) {
        ok = false;
    }
    if (ok && !lrc_ctc_word_spans_to_line_timestamps(&word_spans,
                                                     &normalized,
                                                     &line_timestamps,
                                                     &align_result)) {
        ok = false;
    }

    if (ok && (line_timestamps.line_count > INT32_MAX)) {
        ok = false;
    }
    if (ok) {
        output_lines = malloc2(
            line_timestamps.line_count*SIZEOF(*output_lines)
        );
        if (!ctc_align_output_lines_from_timestamps(&lyrics,
                                                    &line_timestamps,
                                                    output_lines)) {
            ok = false;
        }
    }
    if (ok && !lrc_write_output_file(lrc_path,
                                     output_lines,
                                     (int32)line_timestamps.line_count,
                                     &write_result)) {
        ok = false;
    }
    if (ok) {
        written_lrc = read_entire_file(lrc_path, &written_lrc_len);
        if (!strequal2(written_lrc,
                       written_lrc_len,
                       expected_lrc,
                       strlen32(expected_lrc))) {
            ok = false;
        }
    }

    if (written_lrc) {
        free2(written_lrc, ((int64)written_lrc_len + 1)*SIZEOF(*written_lrc));
    }
    if (output_lines) {
        free2(output_lines,
              line_timestamps.line_count*SIZEOF(*output_lines));
    }
    lrc_ctc_line_timestamps_destroy(&line_timestamps);
    lrc_ctc_word_spans_destroy(&word_spans);
    lrc_ctc_token_spans_destroy(&token_spans);
    lrc_ctc_path_destroy(&path);
    lrc_ctc_trellis_destroy(&trellis);
    lrc_ctc_emissions_destroy(&emissions);
    if (values) {
        free2(values, value_count*SIZEOF(*values));
    }
    if (target_token_ids) {
        free2(target_token_ids, token_count*SIZEOF(*target_token_ids));
    }
    lrc_ctc_tokenized_text_destroy(&tokens);
    lrc_ctc_tokenizer_destroy(&tokenizer);
    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);
    lrc_ctc_model_input_destroy(&input);
    lrc_ctc_audio_destroy(&audio);
    test_remove_tree(temp_dir);

    if (!ok) {
        return ctc_align_test_fail("full synthetic lrc pipeline");
    }

    return 0;
}

static int32
ctc_align_test_maxwell_fixture_lrc_pipeline(void) {
    LrcCtcAudioConfig audio_config;
    LrcCtcAudioResult audio_result;
    LrcCtcAudio audio;
    LrcCtcModelConfig model_config;
    LrcCtcModelInputResult model_result;
    LrcCtcModelInput input;
    LrcLyrics lyrics;
    LrcLyricsLoadResult lyrics_result;
    LrcLyricsNormalized normalized;
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizedText tokens;
    LrcCtcTokenizeResult tokenize_result;
    LrcCtcFakeInference fake;
    LrcCtcInferenceBackend backend;
    LrcCtcInferenceResult inference_result;
    LrcCtcEmissions emissions;
    LrcCtcAlignResult align_result;
    LrcCtcTrellis trellis;
    LrcCtcPath path;
    LrcCtcTokenSpans token_spans;
    LrcCtcWordSpans word_spans;
    LrcCtcLineTimestamps line_timestamps;
    LrcParsedFile expected_lrc;
    LrcParsedFile actual_lrc;
    LrcOutputLine *output_lines;
    LrcWriteResult write_result;
    char *lyrics_path;
    char *vocals_path;
    char *expected_lrc_path;
    char *expected_lrc_text;
    char *actual_lrc_text;
    int32 expected_lrc_text_len;
    int32 actual_lrc_text_len;
    char temp_dir[PATH_MAX];
    char output_lrc_path[PATH_MAX];
    int32 *target_token_ids;
    int64 *token_frames;
    float *values;
    float frame_duration_seconds;
    int64 token_count;
    int64 frame_count;
    int64 vocabulary_size;
    int64 value_count;
    bool ok;

    lyrics_path = getenv("LRC_TEST_MAXWELL_TXT");
    if (lyrics_path == NULL) {
        lyrics_path = "next-phase/maxwell.txt";
    }
    vocals_path = getenv("LRC_TEST_MAXWELL_VOCALS");
    if (vocals_path == NULL) {
        vocals_path = "next-phase/maxwell_vocals.opus";
    }
    expected_lrc_path = getenv("LRC_TEST_MAXWELL_LRC");
    if (expected_lrc_path == NULL) {
        expected_lrc_path = "next-phase/maxwell.lrc";
    }
    if (!test_command_exists("ffmpeg") || !util_file_exists(lyrics_path)
        || !util_file_exists(vocals_path)
        || !util_file_exists(expected_lrc_path)) {
        return 0;
    }

    lrc_ctc_audio_init(&audio);
    lrc_ctc_model_input_init(&input);
    lrc_lyrics_init(&lyrics);
    lrc_lyrics_normalized_init(&normalized);
    lrc_ctc_tokenizer_init(&tokenizer);
    lrc_ctc_tokenized_text_init(&tokens);
    lrc_ctc_fake_inference_init(&fake);
    lrc_ctc_emissions_init(&emissions);
    lrc_ctc_trellis_init(&trellis);
    lrc_ctc_path_init(&path);
    lrc_ctc_token_spans_init(&token_spans);
    lrc_ctc_word_spans_init(&word_spans);
    lrc_ctc_line_timestamps_init(&line_timestamps);
    lrc_parsed_file_init(&expected_lrc);
    lrc_parsed_file_init(&actual_lrc);

    output_lines = NULL;
    expected_lrc_text = NULL;
    actual_lrc_text = NULL;
    expected_lrc_text_len = 0;
    actual_lrc_text_len = 0;
    target_token_ids = NULL;
    token_frames = NULL;
    values = NULL;
    frame_count = 0;
    vocabulary_size = 0;
    value_count = 0;
    ok = true;

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "ctc_maxwell_lrc");
    ctc_align_join_path(output_lrc_path,
                        SIZEOF(output_lrc_path),
                        temp_dir,
                        "generated.lrc");

    if (!ctc_align_parse_lrc_file(&expected_lrc,
                                  expected_lrc_path,
                                  &expected_lrc_text,
                                  &expected_lrc_text_len)) {
        ok = false;
    }

    lrc_ctc_audio_config_init(&audio_config);
    audio_config.sample_rate = LRC_CTC_AUDIO_DEFAULT_SAMPLE_RATE;
    if (ok && !lrc_ctc_audio_decode_file(&audio,
                                         vocals_path,
                                         &audio_config,
                                         &audio_result)) {
        ok = false;
    }
    if (ok && ((audio.sample_rate != LRC_CTC_AUDIO_DEFAULT_SAMPLE_RATE)
               || (audio.channel_count != 1)
               || (audio.sample_count <= 0))) {
        ok = false;
    }

    lrc_ctc_model_config_init(&model_config);
    if (ok && !lrc_ctc_model_input_prepare(&input,
                                           &audio,
                                           &model_config,
                                           &model_result)) {
        ok = false;
    }
    frame_duration_seconds = (float)(input.stride_ms/1000.0);

    if (ok && !lrc_lyrics_load_file(&lyrics, lyrics_path, &lyrics_result)) {
        ok = false;
    }
    if (ok && !lrc_lyrics_normalize(&lyrics, &normalized)) {
        ok = false;
    }
    if (ok && !ctc_align_load_alphabet_tokenizer(&tokenizer)) {
        ok = false;
    }
    if (ok && !lrc_ctc_tokenizer_tokenize_normalized(&tokenizer,
                                                     &normalized,
                                                     &tokens,
                                                     &tokenize_result)) {
        ok = false;
    }

    token_count = tokens.token_count;
    if (ok && (token_count <= 0)) {
        ok = false;
    }
    if (ok) {
        target_token_ids = malloc2(token_count*SIZEOF(*target_token_ids));
        token_frames = malloc2(token_count*SIZEOF(*token_frames));
        for (int64 i = 0; i < token_count; i += 1) {
            target_token_ids[i] = tokens.tokens[i].token_id;
        }
        if (!ctc_align_make_line_timed_token_frames(&expected_lrc,
                                                    &tokens,
                                                    frame_duration_seconds,
                                                    token_frames,
                                                    &frame_count)) {
            ok = false;
        }
    }
    if (ok && ((frame_count <= 0)
               || ((double)frame_count*(double)frame_duration_seconds
                   > audio.duration_seconds + 0.5))) {
        ok = false;
    }

    vocabulary_size = tokenizer.token_count;
    if (ok && (frame_count > INT64_MAX/vocabulary_size)) {
        ok = false;
    }
    if (ok) {
        value_count = frame_count*vocabulary_size;
        values = malloc2(value_count*SIZEOF(*values));
        ctc_align_fill_token_frame_values(values,
                                          frame_count,
                                          vocabulary_size,
                                          tokenizer.blank_id,
                                          target_token_ids,
                                          token_frames,
                                          token_count);
    }

    if (ok && !lrc_ctc_fake_inference_set(&fake,
                                          values,
                                          frame_count,
                                          vocabulary_size)) {
        ok = false;
    }
    if (ok) {
        lrc_ctc_fake_inference_backend(&fake, &backend);
        if (!lrc_ctc_inference_run(&backend,
                                   &input,
                                   &emissions,
                                   &inference_result)) {
            ok = false;
        }
    }
    if (ok && !lrc_ctc_emissions_convert_to_log_probabilities(
        &emissions,
        LRC_CTC_EMISSION_VALUES_LOG_PROBABILITIES,
        &inference_result
    )) {
        ok = false;
    }

    if (ok && !lrc_ctc_trellis_score_forward(&trellis,
                                             &emissions,
                                             target_token_ids,
                                             token_count,
                                             tokenizer.blank_id,
                                             &align_result)) {
        ok = false;
    }
    if (ok && !lrc_ctc_trellis_backtrack(&trellis,
                                         &emissions,
                                         target_token_ids,
                                         token_count,
                                         tokenizer.blank_id,
                                         &path,
                                         &align_result)) {
        ok = false;
    }
    if (ok && !lrc_ctc_path_to_token_spans(&path,
                                           &emissions,
                                           frame_duration_seconds,
                                           &token_spans,
                                           &align_result)) {
        ok = false;
    }
    if (ok && !lrc_ctc_token_spans_to_word_spans(&token_spans,
                                                 &tokens,
                                                 &normalized,
                                                 &word_spans,
                                                 &align_result)) {
        ok = false;
    }
    if (ok && !lrc_ctc_word_spans_to_line_timestamps(&word_spans,
                                                     &normalized,
                                                     &line_timestamps,
                                                     &align_result)) {
        ok = false;
    }

    if (ok && (line_timestamps.line_count > INT32_MAX)) {
        ok = false;
    }
    if (ok) {
        output_lines = malloc2(
            line_timestamps.line_count*SIZEOF(*output_lines)
        );
        if (!ctc_align_output_lines_from_timestamps(&lyrics,
                                                    &line_timestamps,
                                                    output_lines)) {
            ok = false;
        }
    }
    if (ok && !lrc_write_output_file(output_lrc_path,
                                     output_lines,
                                     (int32)line_timestamps.line_count,
                                     &write_result)) {
        ok = false;
    }
    if (ok && !ctc_align_parse_lrc_file(&actual_lrc,
                                        output_lrc_path,
                                        &actual_lrc_text,
                                        &actual_lrc_text_len)) {
        ok = false;
    }
    if (ok && !ctc_align_parsed_files_close(&actual_lrc,
                                            &expected_lrc,
                                            0.015f)) {
        ok = false;
    }

    if (actual_lrc_text) {
        free2(actual_lrc_text,
              ((int64)actual_lrc_text_len + 1)*SIZEOF(*actual_lrc_text));
    }
    if (expected_lrc_text) {
        free2(expected_lrc_text,
              ((int64)expected_lrc_text_len + 1)*SIZEOF(*expected_lrc_text));
    }
    if (output_lines) {
        free2(output_lines,
              line_timestamps.line_count*SIZEOF(*output_lines));
    }
    if (values) {
        free2(values, value_count*SIZEOF(*values));
    }
    if (token_frames) {
        free2(token_frames, token_count*SIZEOF(*token_frames));
    }
    if (target_token_ids) {
        free2(target_token_ids, token_count*SIZEOF(*target_token_ids));
    }
    lrc_parsed_file_destroy(&actual_lrc);
    lrc_parsed_file_destroy(&expected_lrc);
    lrc_ctc_line_timestamps_destroy(&line_timestamps);
    lrc_ctc_word_spans_destroy(&word_spans);
    lrc_ctc_token_spans_destroy(&token_spans);
    lrc_ctc_path_destroy(&path);
    lrc_ctc_trellis_destroy(&trellis);
    lrc_ctc_emissions_destroy(&emissions);
    lrc_ctc_tokenized_text_destroy(&tokens);
    lrc_ctc_tokenizer_destroy(&tokenizer);
    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);
    lrc_ctc_model_input_destroy(&input);
    lrc_ctc_audio_destroy(&audio);
    test_remove_tree(temp_dir);

    if (!ok) {
        return ctc_align_test_fail("maxwell fixture lrc pipeline");
    }

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
    if (ctc_align_test_graph_build_layout() != 0) {
        exit(1);
    }
    if (ctc_align_test_graph_rejects_bad_inputs() != 0) {
        exit(1);
    }
    if (ctc_align_test_graph_transition_rules() != 0) {
        exit(1);
    }
    if (ctc_align_test_required_frame_count_for_tokens() != 0) {
        exit(1);
    }
    if (ctc_align_test_score_rejects_too_few_repeated_frames() != 0) {
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
    if (ctc_align_test_trellis_uses_graph_state_columns() != 0) {
        exit(1);
    }
    if (ctc_align_test_forward_scores_ctc_skip_transition() != 0) {
        exit(1);
    }
    if (ctc_align_test_best_final_state_selection() != 0) {
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
    if (ctc_align_test_token_spans_reject_out_of_order_targets() != 0) {
        exit(1);
    }
    if (ctc_align_test_word_spans_group_generated_words() != 0) {
        exit(1);
    }
    if (ctc_align_test_word_spans_use_skipped_space_gaps() != 0) {
        exit(1);
    }
    if (ctc_align_test_word_spans_handle_removed_punctuation() != 0) {
        exit(1);
    }
    if (ctc_align_test_word_spans_reject_bad_inputs() != 0) {
        exit(1);
    }
    if (ctc_align_test_line_timestamps_from_generated_words() != 0) {
        exit(1);
    }
    if (ctc_align_test_line_timestamps_reject_bad_inputs() != 0) {
        exit(1);
    }
    if (ctc_align_test_maxwell_line_timestamp_comparison() != 0) {
        exit(1);
    }
    if (ctc_align_test_full_synthetic_alignment_pipeline() != 0) {
        exit(1);
    }
    if (ctc_align_test_full_synthetic_lrc_pipeline() != 0) {
        exit(1);
    }
    if (ctc_align_test_maxwell_fixture_lrc_pipeline() != 0) {
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
