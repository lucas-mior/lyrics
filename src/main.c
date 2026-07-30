#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <onnxruntime_c_api.h>

#define ARRAY_LENGTH(a) (sizeof(a) / sizeof((a)[0]))

static const OrtApi *ort;

static int
ort_report_status(OrtStatus *status, const char *expression) {
    const char *message;

    if (status == NULL) {
        return 0;
    }

    message = ort->GetErrorMessage(status);
    fprintf(stderr, "ONNX Runtime error in %s: %s\n", expression, message);
    ort->ReleaseStatus(status);

    return -1;
}

#define ORT_CHECK(expression)                                              \
    do {                                                                   \
        if (ort_report_status((expression), #expression) != 0) {           \
            goto cleanup;                                                  \
        }                                                                  \
    } while (0)

static int
verify_output(OrtValue *output_tensor, const float *expected,
              size_t expected_count) {
    OrtTensorTypeAndShapeInfo *shape_info = NULL;
    ONNXTensorElementDataType element_type;
    size_t dimension_count;
    size_t element_count;
    int64_t dimensions[2];
    void *raw_output;
    float *output;
    int result = -1;

    ORT_CHECK(ort->GetTensorTypeAndShape(output_tensor, &shape_info));
    ORT_CHECK(ort->GetTensorElementType(shape_info, &element_type));
    ORT_CHECK(ort->GetDimensionsCount(shape_info, &dimension_count));

    if (element_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        fprintf(stderr, "Unexpected output element type: %d\n", element_type);
        goto cleanup;
    }
    if (dimension_count != ARRAY_LENGTH(dimensions)) {
        fprintf(stderr, "Unexpected output rank: %zu\n", dimension_count);
        goto cleanup;
    }

    ORT_CHECK(ort->GetDimensions(shape_info, dimensions,
                                 ARRAY_LENGTH(dimensions)));
    ORT_CHECK(ort->GetTensorShapeElementCount(shape_info, &element_count));

    if (dimensions[0] != 1 || dimensions[1] != (int64_t)expected_count) {
        fprintf(stderr, "Unexpected output shape: [%lld, %lld]\n",
                (long long)dimensions[0], (long long)dimensions[1]);
        goto cleanup;
    }
    if (element_count != expected_count) {
        fprintf(stderr, "Unexpected output element count: %zu\n", element_count);
        goto cleanup;
    }

    raw_output = NULL;
    ORT_CHECK(ort->GetTensorMutableData(output_tensor, &raw_output));
    output = raw_output;

    for (size_t i = 0; i < expected_count; i += 1) {
        if (fabsf(output[i] - expected[i]) > 1.0e-6f) {
            fprintf(stderr,
                    "Output mismatch at index %zu: got %.9g, expected %.9g\n",
                    i, output[i], expected[i]);
            goto cleanup;
        }
    }

    printf("input :");
    for (size_t i = 0; i < expected_count; i += 1) {
        printf(" %.9g", expected[i]);
    }
    printf("\noutput:");
    for (size_t i = 0; i < expected_count; i += 1) {
        printf(" %.9g", output[i]);
    }
    printf("\nidentity model verification passed\n");

    result = 0;

cleanup:
    if (shape_info != NULL) {
        ort->ReleaseTensorTypeAndShapeInfo(shape_info);
    }

    return result;
}

static void
print_usage(const char *program) {
    fprintf(stderr, "usage: %s [model.onnx]\n", program);
}

int
main(int argc, char **argv) {
    const OrtApiBase *api_base;
    const char *model_path;
    const char *input_names[] = {"input"};
    const char *output_names[] = {"output"};
    const int64_t input_shape[] = {1, 4};
    float input_data[] = {1.0f, -2.5f, 3.25f, 8.0f};
    const OrtValue *input_values[1];
    OrtEnv *env = NULL;
    OrtSessionOptions *session_options = NULL;
    OrtSession *session = NULL;
    OrtMemoryInfo *memory_info = NULL;
    OrtValue *input_tensor = NULL;
    OrtValue *output_tensor = NULL;
    size_t input_count;
    size_t output_count;
    int is_tensor;
    int result = EXIT_FAILURE;

    if (argc > 2) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    model_path = argc == 2 ? argv[1] : "models/identity.onnx";

    api_base = OrtGetApiBase();
    if (api_base == NULL) {
        fprintf(stderr, "OrtGetApiBase returned NULL\n");
        goto cleanup;
    }

    ort = api_base->GetApi(ORT_API_VERSION);
    if (ort == NULL) {
        fprintf(stderr, "ONNX Runtime does not support API version %d\n",
                ORT_API_VERSION);
        goto cleanup;
    }

    ORT_CHECK(ort->CreateEnv(ORT_LOGGING_LEVEL_WARNING,
                             "ort-c-identity", &env));
    ORT_CHECK(ort->CreateSessionOptions(&session_options));
    ORT_CHECK(ort->SetSessionGraphOptimizationLevel(
        session_options, ORT_ENABLE_BASIC));
    ORT_CHECK(ort->CreateSession(env, model_path, session_options, &session));

    ORT_CHECK(ort->SessionGetInputCount(session, &input_count));
    ORT_CHECK(ort->SessionGetOutputCount(session, &output_count));
    if (input_count != 1 || output_count != 1) {
        fprintf(stderr, "Expected one input and one output, got %zu and %zu\n",
                input_count, output_count);
        goto cleanup;
    }

    ORT_CHECK(ort->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault,
                                       &memory_info));
    ORT_CHECK(ort->CreateTensorWithDataAsOrtValue(
        memory_info, input_data, sizeof(input_data), input_shape,
        ARRAY_LENGTH(input_shape), ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
        &input_tensor));
    ORT_CHECK(ort->IsTensor(input_tensor, &is_tensor));
    if (!is_tensor) {
        fprintf(stderr, "Input OrtValue is not a tensor\n");
        goto cleanup;
    }

    input_values[0] = input_tensor;
    ORT_CHECK(ort->Run(session, NULL, input_names, input_values,
                       ARRAY_LENGTH(input_values), output_names,
                       ARRAY_LENGTH(output_names), &output_tensor));
    ORT_CHECK(ort->IsTensor(output_tensor, &is_tensor));
    if (!is_tensor) {
        fprintf(stderr, "Output OrtValue is not a tensor\n");
        goto cleanup;
    }

    if (verify_output(output_tensor, input_data, ARRAY_LENGTH(input_data)) != 0) {
        goto cleanup;
    }

    result = EXIT_SUCCESS;

cleanup:
    if (output_tensor != NULL) {
        ort->ReleaseValue(output_tensor);
    }
    if (input_tensor != NULL) {
        ort->ReleaseValue(input_tensor);
    }
    if (memory_info != NULL) {
        ort->ReleaseMemoryInfo(memory_info);
    }
    if (session != NULL) {
        ort->ReleaseSession(session);
    }
    if (session_options != NULL) {
        ort->ReleaseSessionOptions(session_options);
    }
    if (env != NULL) {
        ort->ReleaseEnv(env);
    }

    return result;
}
