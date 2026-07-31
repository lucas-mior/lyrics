#include "ctc_inference.h"

#include "cbase.h"

#if !defined(TESTING_ctc_inference)
#define TESTING_ctc_inference 0
#endif

#if LRC_CTC_INFERENCE_ENABLE_ORT && TESTING_ctc_inference
#include <ort.c>
#endif

static void
lrc_ctc_inference_result_init(LrcCtcInferenceResult *result) {
    if (result == NULL) {
        return;
    }

    result->error = LRC_CTC_INFERENCE_ERROR_NONE;
    result->message = "ok";

    result->output_index = -1;

    return;
}

static void
lrc_ctc_inference_result_set(
    LrcCtcInferenceResult *result,
    enum LrcCtcInferenceError error,
    char *message,
    int64 output_index
) {
    if (result == NULL) {
        return;
    }

    result->error = error;
    result->message = message;

    result->output_index = output_index;

    return;
}

static void
lrc_ctc_emissions_init(LrcCtcEmissions *emissions) {
    if (emissions == NULL) {
        return;
    }

    memset64(emissions, 0, SIZEOF(*emissions));

    return;
}

static void
lrc_ctc_emissions_destroy(LrcCtcEmissions *emissions) {
    if (emissions == NULL) {
        return;
    }

    if (emissions->values) {
        free2(emissions->values,
              emissions->value_count*SIZEOF(*emissions->values));
    }

    lrc_ctc_emissions_init(emissions);

    return;
}

static bool
lrc_ctc_emissions_shape_valid(
    int64 *shape,
    int32 shape_len,
    int64 *row_count,
    int64 *row_frame_count,
    int64 *frame_count,
    int64 *vocabulary_size,
    int64 *value_count,
    LrcCtcInferenceResult *result
) {
    int64 rows;
    int64 row_frames;
    int64 frames;
    int64 vocab;
    int64 count;

    if ((shape == NULL) || (shape_len < 2)
        || (shape_len > LRC_CTC_EMISSIONS_MAX_RANK)) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_INVALID_OUTPUT,
            "CTC emissions must have rank 2 or rank 3",
            -1
        );
        return false;
    }

    if (shape_len == 2) {
        rows = 1;
        row_frames = shape[0];
        vocab = shape[1];
    } else {
        rows = shape[0];
        row_frames = shape[1];
        vocab = shape[2];
    }
    if ((rows <= 0) || (row_frames <= 0) || (vocab <= 0)) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_INVALID_OUTPUT,
            "CTC emissions have invalid dimensions",
            -1
        );
        return false;
    }
    if (rows > INT64_MAX/row_frames) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_OUTPUT_TOO_LARGE,
            "CTC emissions frame count is too large",
            -1
        );
        return false;
    }
    frames = rows*row_frames;
    if (frames > INT64_MAX/vocab) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_OUTPUT_TOO_LARGE,
            "CTC emissions tensor is too large",
            -1
        );
        return false;
    }

    count = frames*vocab;
    *row_count = rows;
    *row_frame_count = row_frames;
    *frame_count = frames;
    *vocabulary_size = vocab;
    *value_count = count;

    return true;
}

static bool
lrc_ctc_emissions_values_valid(
    float *values,
    int64 value_count,
    LrcCtcInferenceResult *result
) {
    if ((values == NULL) || (value_count <= 0)) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_INVALID_OUTPUT,
            "CTC emissions values are missing",
            -1
        );
        return false;
    }

    for (int64 i = 0; i < value_count; i += 1) {
        if (!isfinite((double)values[i])) {
            lrc_ctc_inference_result_set(
                result,
                LRC_CTC_INFERENCE_ERROR_NON_FINITE_OUTPUT,
                "CTC emissions contain a non-finite value",
                i
            );
            return false;
        }
    }

    return true;
}

static bool
lrc_ctc_emissions_copy_shape(
    LrcCtcEmissions *emissions,
    float *values,
    int64 value_count,
    int64 *shape,
    int32 shape_len,
    LrcCtcInferenceResult *result
) {
    int64 row_count;
    int64 row_frame_count;
    int64 frame_count;
    int64 vocabulary_size;
    int64 expected_count;

    if (result) {
        lrc_ctc_inference_result_init(result);
    }
    if (emissions == NULL) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_INVALID_ARGUMENT,
            "CTC emissions destination is missing",
            -1
        );
        return false;
    }

    lrc_ctc_emissions_destroy(emissions);
    if (!lrc_ctc_emissions_shape_valid(shape,
                                       shape_len,
                                       &row_count,
                                       &row_frame_count,
                                       &frame_count,
                                       &vocabulary_size,
                                       &expected_count,
                                       result)) {
        return false;
    }
    if (value_count != expected_count) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_INVALID_OUTPUT,
            "CTC emissions value count does not match shape",
            -1
        );
        return false;
    }
    if (!lrc_ctc_emissions_values_valid(values, value_count, result)) {
        return false;
    }
    if (value_count > INT64_MAX/SIZEOF(*emissions->values)) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_OUTPUT_TOO_LARGE,
            "CTC emissions copy is too large",
            -1
        );
        return false;
    }

    emissions->values = malloc2(value_count*SIZEOF(*emissions->values));
    memcpy64(emissions->values, values, value_count*SIZEOF(*values));
    emissions->value_count = value_count;
    emissions->row_count = row_count;
    emissions->row_frame_count = row_frame_count;
    emissions->frame_count = frame_count;
    emissions->vocabulary_size = vocabulary_size;
    emissions->shape_len = shape_len;
    for (int32 i = 0; i < shape_len; i += 1) {
        emissions->shape[i] = shape[i];
    }

    return true;
}

static bool
lrc_ctc_emissions_ready(
    LrcCtcEmissions *emissions,
    LrcCtcInferenceResult *result
) {
    if (emissions == NULL) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_INVALID_ARGUMENT,
            "CTC emissions are missing",
            -1
        );
        return false;
    }
    if ((emissions->values == NULL) || (emissions->value_count <= 0)
        || (emissions->frame_count <= 0)
        || (emissions->vocabulary_size <= 0)) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_INVALID_OUTPUT,
            "CTC emissions are not prepared",
            -1
        );
        return false;
    }
    if (emissions->frame_count
        > INT64_MAX/emissions->vocabulary_size) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_OUTPUT_TOO_LARGE,
            "CTC emissions dimensions are too large",
            -1
        );
        return false;
    }
    if (emissions->value_count
        != emissions->frame_count*emissions->vocabulary_size) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_INVALID_OUTPUT,
            "CTC emissions value count does not match dimensions",
            -1
        );
        return false;
    }

    return true;
}

static bool
lrc_ctc_emissions_log_softmax_row(
    float *row,
    int64 vocabulary_size,
    int64 row_offset,
    LrcCtcInferenceResult *result
) {
    double sum;
    double log_denom;
    float max_value;

    if ((row == NULL) || (vocabulary_size <= 0)) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_INVALID_ARGUMENT,
            "CTC log-softmax row arguments are invalid",
            row_offset
        );
        return false;
    }

    max_value = row[0];
    for (int64 i = 1; i < vocabulary_size; i += 1) {
        if (row[i] > max_value) {
            max_value = row[i];
        }
    }

    sum = 0.0;
    for (int64 i = 0; i < vocabulary_size; i += 1) {
        sum += exp((double)row[i] - (double)max_value);
    }
    if (!isfinite(sum) || (sum <= 0.0)) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_INVALID_PROBABILITY,
            "CTC log-softmax row has invalid normalizer",
            row_offset
        );
        return false;
    }

    log_denom = (double)max_value + log(sum);
    for (int64 i = 0; i < vocabulary_size; i += 1) {
        row[i] = (float)((double)row[i] - log_denom);
    }

    return true;
}

static bool
lrc_ctc_emissions_log_probabilities_from_probabilities_row(
    float *row,
    int64 vocabulary_size,
    int64 row_offset,
    LrcCtcInferenceResult *result
) {
    if ((row == NULL) || (vocabulary_size <= 0)) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_INVALID_ARGUMENT,
            "CTC probability row arguments are invalid",
            row_offset
        );
        return false;
    }

    for (int64 i = 0; i < vocabulary_size; i += 1) {
        if (!isfinite((double)row[i]) || (row[i] <= 0.0f)) {
            lrc_ctc_inference_result_set(
                result,
                LRC_CTC_INFERENCE_ERROR_INVALID_PROBABILITY,
                "CTC probabilities must be finite and positive",
                row_offset + i
            );
            return false;
        }
    }

    for (int64 i = 0; i < vocabulary_size; i += 1) {
        row[i] = logf(row[i]);
    }

    return true;
}

static bool
lrc_ctc_emissions_convert_to_log_probabilities(
    LrcCtcEmissions *emissions,
    enum LrcCtcEmissionValuesKind values_kind,
    LrcCtcInferenceResult *result
) {
    if (result) {
        lrc_ctc_inference_result_init(result);
    }
    if (!lrc_ctc_emissions_ready(emissions, result)) {
        return false;
    }

    switch (values_kind) {
    case LRC_CTC_EMISSION_VALUES_LOG_PROBABILITIES:
        return true;
    case LRC_CTC_EMISSION_VALUES_LOGITS:
        for (int64 frame = 0; frame < emissions->frame_count; frame += 1) {
            int64 offset = frame*emissions->vocabulary_size;

            if (!lrc_ctc_emissions_log_softmax_row(
                    emissions->values + offset,
                    emissions->vocabulary_size,
                    offset,
                    result)) {
                return false;
            }
        }
        return true;
    case LRC_CTC_EMISSION_VALUES_PROBABILITIES:
        for (int64 frame = 0; frame < emissions->frame_count; frame += 1) {
            int64 offset = frame*emissions->vocabulary_size;

            if (!lrc_ctc_emissions_log_probabilities_from_probabilities_row(
                    emissions->values + offset,
                    emissions->vocabulary_size,
                    offset,
                    result)) {
                return false;
            }
        }
        return true;
    default:
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_INVALID_ARGUMENT,
            "CTC emission value kind is invalid",
            -1
        );
        return false;
    }
}

static bool
lrc_ctc_inference_input_ready(
    LrcCtcModelInput *input,
    LrcCtcInferenceResult *result
) {
    if (input == NULL) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_INVALID_ARGUMENT,
            "CTC inference input is missing",
            -1
        );
        return false;
    }
    if ((input->samples == NULL) || (input->sample_count <= 0)
        || (input->shape_len != LRC_CTC_MODEL_INPUT_RANK)) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_INVALID_INPUT,
            "CTC inference input tensor is not prepared",
            -1
        );
        return false;
    }

    return true;
}

static bool
lrc_ctc_inference_run(
    LrcCtcInferenceBackend *backend,
    LrcCtcModelInput *input,
    LrcCtcEmissions *emissions,
    LrcCtcInferenceResult *result
) {
    if (result) {
        lrc_ctc_inference_result_init(result);
    }
    if ((backend == NULL) || (backend->run == NULL)
        || (emissions == NULL)) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_INVALID_ARGUMENT,
            "CTC inference backend arguments are invalid",
            -1
        );
        return false;
    }
    if (!lrc_ctc_inference_input_ready(input, result)) {
        return false;
    }

    return backend->run(backend->backend, input, emissions, result);
}

static void
lrc_ctc_fake_inference_init(LrcCtcFakeInference *fake) {
    if (fake == NULL) {
        return;
    }

    memset64(fake, 0, SIZEOF(*fake));

    return;
}

static bool
lrc_ctc_fake_inference_set_shape(
    LrcCtcFakeInference *fake,
    float *values,
    int64 value_count,
    int64 *shape,
    int32 shape_len
) {
    int64 row_count;
    int64 row_frame_count;
    int64 frame_count;
    int64 vocabulary_size;
    int64 expected_count;

    if (fake == NULL) {
        return false;
    }
    if (!lrc_ctc_emissions_shape_valid(shape,
                                       shape_len,
                                       &row_count,
                                       &row_frame_count,
                                       &frame_count,
                                       &vocabulary_size,
                                       &expected_count,
                                       NULL)) {
        return false;
    }
    if ((values == NULL) || (value_count != expected_count)) {
        return false;
    }

    fake->values = values;
    fake->value_count = value_count;
    fake->shape_len = shape_len;
    for (int32 i = 0; i < shape_len; i += 1) {
        fake->shape[i] = shape[i];
    }

    (void)row_count;
    (void)row_frame_count;
    (void)frame_count;
    (void)vocabulary_size;

    return true;
}

static bool
lrc_ctc_fake_inference_set(
    LrcCtcFakeInference *fake,
    float *values,
    int64 frame_count,
    int64 vocabulary_size
) {
    int64 shape[2];

    shape[0] = frame_count;
    shape[1] = vocabulary_size;

    if ((frame_count <= 0) || (vocabulary_size <= 0)
        || (frame_count > INT64_MAX/vocabulary_size)) {
        return false;
    }

    return lrc_ctc_fake_inference_set_shape(fake,
                                            values,
                                            frame_count*vocabulary_size,
                                            shape,
                                            2);
}

static bool
lrc_ctc_fake_inference_run(
    void *backend,
    LrcCtcModelInput *input,
    LrcCtcEmissions *emissions,
    LrcCtcInferenceResult *result
) {
    LrcCtcFakeInference *fake;

    (void)input;
    fake = backend;
    if (fake == NULL) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_INVALID_ARGUMENT,
            "fake CTC inference backend is missing",
            -1
        );
        return false;
    }

    return lrc_ctc_emissions_copy_shape(emissions,
                                        fake->values,
                                        fake->value_count,
                                        fake->shape,
                                        fake->shape_len,
                                        result);
}

static void
lrc_ctc_fake_inference_backend(
    LrcCtcFakeInference *fake,
    LrcCtcInferenceBackend *backend
) {
    if (backend == NULL) {
        return;
    }

    backend->backend = fake;
    backend->run = lrc_ctc_fake_inference_run;

    return;
}

static void
lrc_ctc_onnx_inference_init(LrcCtcOnnxInference *onnx) {
    if (onnx == NULL) {
        return;
    }

    memset64(onnx, 0, SIZEOF(*onnx));

    return;
}

static void
lrc_ctc_onnx_inference_destroy(LrcCtcOnnxInference *onnx) {
    if (onnx == NULL) {
        return;
    }

#if LRC_CTC_INFERENCE_ENABLE_ORT
    if (onnx->loaded) {
        ort_model_destroy(&onnx->context, &onnx->model);
        ort_context_destroy(&onnx->context);
    }
#endif

    lrc_ctc_onnx_inference_init(onnx);

    return;
}

#if LRC_CTC_INFERENCE_ENABLE_ORT
static bool
lrc_ctc_onnx_model_input_info(
    LrcCtcOnnxInference *onnx,
    LrcCtcModelIoInfo *info
) {
    OrtModelIoInfo ort_info;

    if ((onnx == NULL) || (info == NULL)) {
        return false;
    }
    if (!ort_model_input_info(&onnx->model, &ort_info)) {
        return false;
    }

    memset64(info, 0, SIZEOF(*info));
    info->count = ort_info.count;
    info->shape_len = ort_info.shape_len;
    info->is_float32 = true;
    for (int32 i = 0; i < info->shape_len; i += 1) {
        info->shape[i] = ort_info.shape[i];
    }

    return true;
}
#endif

static bool
lrc_ctc_onnx_inference_load(
    LrcCtcOnnxInference *onnx,
    char *model_path,
    LrcCtcInferenceResult *result
) {
    if (result) {
        lrc_ctc_inference_result_init(result);
    }
    if ((onnx == NULL) || (model_path == NULL) || (model_path[0] == '\0')) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_INVALID_ARGUMENT,
            "CTC ONNX inference load received invalid arguments",
            -1
        );
        return false;
    }

#if LRC_CTC_INFERENCE_ENABLE_ORT
    lrc_ctc_onnx_inference_destroy(onnx);
    if (!ort_context_init(&onnx->context)) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_MODEL_LOAD_FAILED,
            "could not initialize ONNX Runtime for CTC inference",
            -1
        );
        return false;
    }
    if (!ort_model_load(&onnx->context, &onnx->model, model_path)) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_MODEL_LOAD_FAILED,
            "could not load CTC ONNX model",
            -1
        );
        ort_context_destroy(&onnx->context);
        return false;
    }

    onnx->loaded = true;

    return true;
#else
    (void)onnx;
    (void)model_path;
    lrc_ctc_inference_result_set(
        result,
        LRC_CTC_INFERENCE_ERROR_BACKEND_UNAVAILABLE,
        "CTC ONNX inference backend is not enabled in this build",
        -1
    );
    return false;
#endif
}

static bool
lrc_ctc_onnx_inference_run(
    void *backend,
    LrcCtcModelInput *input,
    LrcCtcEmissions *emissions,
    LrcCtcInferenceResult *result
) {
#if LRC_CTC_INFERENCE_ENABLE_ORT
    LrcCtcOnnxInference *onnx;
    LrcCtcModelInputResult input_result;
    LrcCtcModelIoInfo input_info;
    OrtTensor input_tensor;
    OrtTensor output_tensor;
    bool ok;

    onnx = backend;
    if ((onnx == NULL) || !onnx->loaded) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_BACKEND_UNAVAILABLE,
            "CTC ONNX inference backend is not loaded",
            -1
        );
        return false;
    }
    if (!lrc_ctc_onnx_model_input_info(onnx, &input_info)
        || !lrc_ctc_model_input_validate_model_io(input,
                                                  &input_info,
                                                  &input_result)) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_INVALID_INPUT,
            "CTC model input does not match ONNX model input",
            -1
        );
        return false;
    }

    ort_tensor_init_empty(&input_tensor);
    ort_tensor_init_empty(&output_tensor);
    if (!ort_tensor_create_f32(&onnx->context,
                               &input_tensor,
                               input->samples,
                               input->sample_count,
                               input->shape,
                               input->shape_len)) {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_BACKEND_FAILED,
            "could not create CTC ONNX input tensor",
            -1
        );
        return false;
    }

    ok = ort_model_run_f32(&onnx->context,
                           &onnx->model,
                           &input_tensor,
                           &output_tensor);
    if (ok) {
        ok = lrc_ctc_emissions_copy_shape(emissions,
                                          output_tensor.data,
                                          output_tensor.data_len,
                                          output_tensor.shape,
                                          output_tensor.shape_len,
                                          result);
    } else {
        lrc_ctc_inference_result_set(
            result,
            LRC_CTC_INFERENCE_ERROR_BACKEND_FAILED,
            "could not run CTC ONNX inference",
            -1
        );
    }

    ort_tensor_destroy(&onnx->context, &output_tensor);
    ort_tensor_destroy(&onnx->context, &input_tensor);

    return ok;
#else
    (void)backend;
    (void)input;
    (void)emissions;
    lrc_ctc_inference_result_set(
        result,
        LRC_CTC_INFERENCE_ERROR_BACKEND_UNAVAILABLE,
        "CTC ONNX inference backend is not enabled in this build",
        -1
    );
    return false;
#endif
}

static void
lrc_ctc_onnx_inference_backend(
    LrcCtcOnnxInference *onnx,
    LrcCtcInferenceBackend *backend
) {
    if (backend == NULL) {
        return;
    }

    backend->backend = onnx;
    backend->run = lrc_ctc_onnx_inference_run;

    return;
}

#if TESTING_ctc_inference

#define CBASE_IMPLEMENT
#include "cbase.h"
#include "ctc_model.c"

static int32
ctc_inference_test_fail(char *name) {
    error2("CTC inference test failed: %s\n", name);

    return 1;
}

static bool
ctc_inference_float_close(float a, float b, float max_error) {
    float diff;

    diff = fabsf(a - b);

    return diff <= max_error;
}

static void
ctc_inference_make_input(LrcCtcModelInput *input) {
    static float samples[] = {0.0f, 0.1f, -0.1f, 0.2f};

    lrc_ctc_model_input_init(input);
    input->samples = samples;
    input->sample_count = LENGTH(samples);
    input->shape_len = LRC_CTC_MODEL_INPUT_RANK;
    input->shape[0] = 1;
    input->shape[1] = LENGTH(samples);

    return;
}

static int32
ctc_inference_test_empty_initializers(void) {
    LrcCtcInferenceResult result;
    LrcCtcEmissions emissions;
    LrcCtcFakeInference fake;
    LrcCtcInferenceBackend backend;
    LrcCtcOnnxInference onnx;

    lrc_ctc_inference_result_init(&result);
    lrc_ctc_emissions_init(&emissions);
    lrc_ctc_fake_inference_init(&fake);
    lrc_ctc_fake_inference_backend(&fake, &backend);
    lrc_ctc_onnx_inference_init(&onnx);

    ASSERT(result.error == LRC_CTC_INFERENCE_ERROR_NONE);
    ASSERT(strequal(result.message, "ok"));
    ASSERT(result.output_index == -1);

    ASSERT(emissions.values == NULL);
    ASSERT(emissions.value_count == 0);
    ASSERT(emissions.frame_count == 0);
    ASSERT(emissions.vocabulary_size == 0);

    ASSERT(fake.values == NULL);
    ASSERT(fake.value_count == 0);
    ASSERT(fake.shape_len == 0);

    ASSERT(backend.backend == &fake);
    ASSERT(backend.run == lrc_ctc_fake_inference_run);

    ASSERT(!onnx.loaded);

    return 0;
}

static int32
ctc_inference_test_fake_rank2(void) {
    LrcCtcInferenceBackend backend;
    LrcCtcInferenceResult result;
    LrcCtcFakeInference fake;
    LrcCtcEmissions emissions;
    LrcCtcModelInput input;
    float values[] = {
        -2.0f, -0.1f, -3.0f,
        -1.0f, -4.0f, -0.2f,
    };

    lrc_ctc_fake_inference_init(&fake);
    lrc_ctc_emissions_init(&emissions);
    ctc_inference_make_input(&input);
    if (!lrc_ctc_fake_inference_set(&fake, values, 2, 3)) {
        return ctc_inference_test_fail("set fake rank-2 emissions");
    }
    lrc_ctc_fake_inference_backend(&fake, &backend);

    if (!lrc_ctc_inference_run(&backend, &input, &emissions, &result)) {
        return ctc_inference_test_fail("run fake rank-2 backend");
    }

    ASSERT(result.error == LRC_CTC_INFERENCE_ERROR_NONE);
    ASSERT(emissions.value_count == 6);
    ASSERT(emissions.row_count == 1);
    ASSERT(emissions.row_frame_count == 2);
    ASSERT(emissions.frame_count == 2);
    ASSERT(emissions.vocabulary_size == 3);
    ASSERT(emissions.shape_len == 2);
    ASSERT(emissions.shape[0] == 2);
    ASSERT(emissions.shape[1] == 3);
    ASSERT(emissions.values[0] == values[0]);
    ASSERT(emissions.values[5] == values[5]);

    values[0] = 99.0f;
    ASSERT(emissions.values[0] == -2.0f);

    emissions.values[1] = NAN;
    lrc_ctc_emissions_destroy(&emissions);

    return 0;
}

static int32
ctc_inference_test_fake_rank3(void) {
    LrcCtcInferenceBackend backend;
    LrcCtcInferenceResult result;
    LrcCtcFakeInference fake;
    LrcCtcEmissions emissions;
    LrcCtcModelInput input;
    int64 shape[3];
    float values[12];

    for (int32 i = 0; i < LENGTH(values); i += 1) {
        values[i] = (float)i;
    }
    shape[0] = 2;
    shape[1] = 3;
    shape[2] = 2;

    lrc_ctc_fake_inference_init(&fake);
    lrc_ctc_emissions_init(&emissions);
    ctc_inference_make_input(&input);
    if (!lrc_ctc_fake_inference_set_shape(&fake,
                                          values,
                                          LENGTH(values),
                                          shape,
                                          3)) {
        return ctc_inference_test_fail("set fake rank-3 emissions");
    }
    lrc_ctc_fake_inference_backend(&fake, &backend);

    if (!lrc_ctc_inference_run(&backend, &input, &emissions, &result)) {
        return ctc_inference_test_fail("run fake rank-3 backend");
    }

    ASSERT(emissions.value_count == 12);
    ASSERT(emissions.row_count == 2);
    ASSERT(emissions.row_frame_count == 3);
    ASSERT(emissions.frame_count == 6);
    ASSERT(emissions.vocabulary_size == 2);
    ASSERT(emissions.shape_len == 3);
    ASSERT(emissions.shape[0] == 2);
    ASSERT(emissions.shape[1] == 3);
    ASSERT(emissions.shape[2] == 2);

    lrc_ctc_emissions_destroy(&emissions);

    return 0;
}

static int32
ctc_inference_test_rejects_invalid_inputs(void) {
    LrcCtcInferenceBackend backend;
    LrcCtcInferenceResult result;
    LrcCtcFakeInference fake;
    LrcCtcEmissions emissions;
    LrcCtcModelInput input;
    float values[] = {0.0f, 1.0f};

    lrc_ctc_fake_inference_init(&fake);
    lrc_ctc_emissions_init(&emissions);
    lrc_ctc_fake_inference_backend(&fake, &backend);
    ctc_inference_make_input(&input);

    if (lrc_ctc_inference_run(NULL, &input, &emissions, &result)) {
        return ctc_inference_test_fail("missing backend accepted");
    }
    ASSERT(result.error == LRC_CTC_INFERENCE_ERROR_INVALID_ARGUMENT);

    if (lrc_ctc_inference_run(&backend, NULL, &emissions, &result)) {
        return ctc_inference_test_fail("missing input accepted");
    }
    ASSERT(result.error == LRC_CTC_INFERENCE_ERROR_INVALID_ARGUMENT);

    input.samples = NULL;
    if (lrc_ctc_inference_run(&backend, &input, &emissions, &result)) {
        return ctc_inference_test_fail("unprepared input accepted");
    }
    ASSERT(result.error == LRC_CTC_INFERENCE_ERROR_INVALID_INPUT);
    ctc_inference_make_input(&input);

    if (lrc_ctc_fake_inference_set(&fake, values, 1, 2)) {
        fake.values[1] = NAN;
        if (lrc_ctc_inference_run(&backend, &input, &emissions, &result)) {
            return ctc_inference_test_fail("non-finite emissions accepted");
        }
        ASSERT(result.error == LRC_CTC_INFERENCE_ERROR_NON_FINITE_OUTPUT);
        ASSERT(result.output_index == 1);
    } else {
        return ctc_inference_test_fail("set non-finite fake emissions");
    }

    return 0;
}

static int32
ctc_inference_test_rejects_bad_shapes(void) {
    LrcCtcFakeInference fake;
    int64 shape[3];
    float values[] = {0.0f, 1.0f, 2.0f, 3.0f};

    lrc_ctc_fake_inference_init(&fake);

    shape[0] = 0;
    shape[1] = 2;
    if (lrc_ctc_fake_inference_set_shape(&fake, values, 0, shape, 2)) {
        return ctc_inference_test_fail("zero shape accepted");
    }

    shape[0] = 2;
    shape[1] = 2;
    if (lrc_ctc_fake_inference_set_shape(&fake, values, 3, shape, 2)) {
        return ctc_inference_test_fail("mismatched value count accepted");
    }

    shape[0] = 1;
    shape[1] = 2;
    shape[2] = 2;
    if (!lrc_ctc_fake_inference_set_shape(&fake, values, 4, shape, 3)) {
        return ctc_inference_test_fail("valid rank-3 fake shape rejected");
    }

    return 0;
}

static int32
ctc_inference_test_log_probability_bypass(void) {
    LrcCtcInferenceResult result;
    LrcCtcEmissions emissions;
    int64 shape[2];
    float values[] = {
        -0.10f, -2.30f,
        -1.20f, -0.40f,
    };

    shape[0] = 2;
    shape[1] = 2;

    lrc_ctc_emissions_init(&emissions);
    if (!lrc_ctc_emissions_copy_shape(&emissions,
                                      values,
                                      LENGTH(values),
                                      shape,
                                      2,
                                      &result)) {
        return ctc_inference_test_fail("copy log-probability emissions");
    }
    if (!lrc_ctc_emissions_convert_to_log_probabilities(
            &emissions,
            LRC_CTC_EMISSION_VALUES_LOG_PROBABILITIES,
            &result)) {
        return ctc_inference_test_fail("bypass log probabilities");
    }

    ASSERT(result.error == LRC_CTC_INFERENCE_ERROR_NONE);
    for (int32 i = 0; i < LENGTH(values); i += 1) {
        ASSERT(emissions.values[i] == values[i]);
    }

    lrc_ctc_emissions_destroy(&emissions);

    return 0;
}

static int32
ctc_inference_test_logits_to_log_probabilities(void) {
    LrcCtcInferenceResult result;
    LrcCtcEmissions emissions;
    int64 shape[2];
    double row1_norm;
    double row2_norm;
    double row1_sum;
    double row2_sum;
    float values[] = {
        0.0f, 0.0f,
        1000.0f, 999.0f,
    };

    shape[0] = 2;
    shape[1] = 2;
    row1_norm = log(2.0);
    row2_norm = log(1.0 + exp(-1.0));

    lrc_ctc_emissions_init(&emissions);
    if (!lrc_ctc_emissions_copy_shape(&emissions,
                                      values,
                                      LENGTH(values),
                                      shape,
                                      2,
                                      &result)) {
        return ctc_inference_test_fail("copy logits emissions");
    }
    if (!lrc_ctc_emissions_convert_to_log_probabilities(
            &emissions,
            LRC_CTC_EMISSION_VALUES_LOGITS,
            &result)) {
        return ctc_inference_test_fail("convert logits to log probabilities");
    }

    ASSERT(result.error == LRC_CTC_INFERENCE_ERROR_NONE);
    ASSERT(ctc_inference_float_close(emissions.values[0],
                                     (float)-row1_norm,
                                     0.00001f));
    ASSERT(ctc_inference_float_close(emissions.values[1],
                                     (float)-row1_norm,
                                     0.00001f));
    ASSERT(ctc_inference_float_close(emissions.values[2],
                                     (float)-row2_norm,
                                     0.00001f));
    ASSERT(ctc_inference_float_close(emissions.values[3],
                                     (float)(-1.0 - row2_norm),
                                     0.00001f));

    row1_sum = exp((double)emissions.values[0])
               + exp((double)emissions.values[1]);
    row2_sum = exp((double)emissions.values[2])
               + exp((double)emissions.values[3]);
    ASSERT(fabs(row1_sum - 1.0) < 0.000001);
    ASSERT(fabs(row2_sum - 1.0) < 0.000001);

    lrc_ctc_emissions_destroy(&emissions);

    return 0;
}

static int32
ctc_inference_test_probabilities_to_log_probabilities(void) {
    LrcCtcInferenceResult result;
    LrcCtcEmissions emissions;
    int64 shape[2];
    float values[] = {
        0.25f, 0.75f,
        0.90f, 0.10f,
    };

    shape[0] = 2;
    shape[1] = 2;

    lrc_ctc_emissions_init(&emissions);
    if (!lrc_ctc_emissions_copy_shape(&emissions,
                                      values,
                                      LENGTH(values),
                                      shape,
                                      2,
                                      &result)) {
        return ctc_inference_test_fail("copy probability emissions");
    }
    if (!lrc_ctc_emissions_convert_to_log_probabilities(
            &emissions,
            LRC_CTC_EMISSION_VALUES_PROBABILITIES,
            &result)) {
        return ctc_inference_test_fail("convert probabilities");
    }

    ASSERT(result.error == LRC_CTC_INFERENCE_ERROR_NONE);
    ASSERT(ctc_inference_float_close(emissions.values[0],
                                     logf(values[0]),
                                     0.00001f));
    ASSERT(ctc_inference_float_close(emissions.values[1],
                                     logf(values[1]),
                                     0.00001f));
    ASSERT(ctc_inference_float_close(emissions.values[2],
                                     logf(values[2]),
                                     0.00001f));
    ASSERT(ctc_inference_float_close(emissions.values[3],
                                     logf(values[3]),
                                     0.00001f));

    lrc_ctc_emissions_destroy(&emissions);

    return 0;
}

static int32
ctc_inference_test_rejects_invalid_probability_conversion(void) {
    LrcCtcInferenceResult result;
    LrcCtcEmissions emissions;
    int64 shape[2];
    float values[] = {1.0f, 0.0f};

    shape[0] = 1;
    shape[1] = 2;
    lrc_ctc_emissions_init(&emissions);
    if (!lrc_ctc_emissions_copy_shape(&emissions,
                                      values,
                                      LENGTH(values),
                                      shape,
                                      2,
                                      &result)) {
        return ctc_inference_test_fail("copy invalid probabilities");
    }
    if (lrc_ctc_emissions_convert_to_log_probabilities(
            &emissions,
            LRC_CTC_EMISSION_VALUES_PROBABILITIES,
            &result)) {
        return ctc_inference_test_fail("zero probability accepted");
    }
    ASSERT(result.error == LRC_CTC_INFERENCE_ERROR_INVALID_PROBABILITY);
    ASSERT(result.output_index == 1);

    values[1] = 1.0f;
    if (!lrc_ctc_emissions_copy_shape(&emissions,
                                      values,
                                      LENGTH(values),
                                      shape,
                                      2,
                                      &result)) {
        return ctc_inference_test_fail("copy valid probabilities");
    }
    if (lrc_ctc_emissions_convert_to_log_probabilities(
            &emissions,
            (enum LrcCtcEmissionValuesKind)777,
            &result)) {
        return ctc_inference_test_fail("invalid value kind accepted");
    }
    ASSERT(result.error == LRC_CTC_INFERENCE_ERROR_INVALID_ARGUMENT);

    lrc_ctc_emissions_destroy(&emissions);

    return 0;
}

static int32
ctc_inference_test_optional_onnx_backend(void) {
#if LRC_CTC_INFERENCE_ENABLE_ORT
    LrcCtcInferenceBackend backend;
    LrcCtcInferenceResult result;
    LrcCtcOnnxInference onnx;
    LrcCtcEmissions emissions;
    LrcCtcModelInput input;
    char *model_path;

    model_path = getenv("LRC_TEST_CTC_MODEL");
    if ((model_path == NULL) || (model_path[0] == '\0')) {
        return 0;
    }

    lrc_ctc_onnx_inference_init(&onnx);
    lrc_ctc_emissions_init(&emissions);
    ctc_inference_make_input(&input);
    if (!lrc_ctc_onnx_inference_load(&onnx, model_path, &result)) {
        return ctc_inference_test_fail("load optional ONNX CTC model");
    }
    lrc_ctc_onnx_inference_backend(&onnx, &backend);
    if (!lrc_ctc_inference_run(&backend, &input, &emissions, &result)) {
        lrc_ctc_onnx_inference_destroy(&onnx);
        return ctc_inference_test_fail("run optional ONNX CTC model");
    }

    ASSERT(emissions.frame_count > 0);
    ASSERT(emissions.vocabulary_size > 0);

    lrc_ctc_emissions_destroy(&emissions);
    lrc_ctc_onnx_inference_destroy(&onnx);
#else
    LrcCtcInferenceResult result;
    LrcCtcOnnxInference onnx;

    lrc_ctc_onnx_inference_init(&onnx);
    if (lrc_ctc_onnx_inference_load(&onnx, "missing.onnx", &result)) {
        return ctc_inference_test_fail("disabled ONNX backend loaded");
    }
    ASSERT(result.error == LRC_CTC_INFERENCE_ERROR_BACKEND_UNAVAILABLE);
#endif

    return 0;
}

int32
main(void) {
    if (ctc_inference_test_empty_initializers() != 0) {
        exit(1);
    }
    if (ctc_inference_test_fake_rank2() != 0) {
        exit(1);
    }
    if (ctc_inference_test_fake_rank3() != 0) {
        exit(1);
    }
    if (ctc_inference_test_rejects_invalid_inputs() != 0) {
        exit(1);
    }
    if (ctc_inference_test_rejects_bad_shapes() != 0) {
        exit(1);
    }
    if (ctc_inference_test_log_probability_bypass() != 0) {
        exit(1);
    }
    if (ctc_inference_test_logits_to_log_probabilities() != 0) {
        exit(1);
    }
    if (ctc_inference_test_probabilities_to_log_probabilities() != 0) {
        exit(1);
    }
    if (ctc_inference_test_rejects_invalid_probability_conversion() != 0) {
        exit(1);
    }
    if (ctc_inference_test_optional_onnx_backend() != 0) {
        exit(1);
    }

    exit(0);
}

#endif /* TESTING_ctc_inference */
