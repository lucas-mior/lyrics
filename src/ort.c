#include "ort.h"

#if !defined(TESTING_ort)
#define TESTING_ort 0
#endif

#include "../cbase/base_macros.h"

#include <limits.h>
#include <onnxruntime_c_api.h>
#include <stdio.h>

static bool
ort_check(OrtContext *context, OrtStatus *status, char *operation) {
    OrtApi *api;

    if (status == NULL) {
        return true;
    }

    api = (OrtApi *)context->api;
    fprintf(stderr, "%s: %s\n", operation, api->GetErrorMessage(status));
    api->ReleaseStatus(status);

    return false;
}

static bool
ort_model_read_tensor_info(
    OrtContext *context,
    OrtSession *session,
    bool input,
    int64 *shape,
    int32 *shape_len
) {
    OrtApi *api;
    OrtTypeInfo *type_info;
    OrtStatus *status;
    ONNXTensorElementDataType element_type;
    OrtTensorTypeAndShapeInfo const *tensor_info;
    int64_t dims[ORT_TENSOR_MAX_RANK];
    size_t dim_count;

    api = (OrtApi *)context->api;
    type_info = NULL;
    if (input) {
        status = api->SessionGetInputTypeInfo(session, 0, &type_info);
        if (!ort_check(context, status, "getting ONNX input type info")) {
            return false;
        }
    } else {
        status = api->SessionGetOutputTypeInfo(session, 0, &type_info);
        if (!ort_check(context, status, "getting ONNX output type info")) {
            return false;
        }
    }

    status = api->CastTypeInfoToTensorInfo(type_info, &tensor_info);
    if (!ort_check(context, status, "casting ONNX type info to tensor info")) {
        api->ReleaseTypeInfo(type_info);
        return false;
    }
    if (tensor_info == NULL) {
        fprintf(stderr, "ONNX model I/O is not a tensor\n");
        api->ReleaseTypeInfo(type_info);
        return false;
    }

    status = api->GetTensorElementType(tensor_info, &element_type);
    if (!ort_check(context, status, "getting ONNX tensor element type")) {
        api->ReleaseTypeInfo(type_info);
        return false;
    }
    if (element_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        fprintf(stderr, "ONNX model I/O tensor is not float32\n");
        api->ReleaseTypeInfo(type_info);
        return false;
    }

    status = api->GetDimensionsCount(tensor_info, &dim_count);
    if (!ort_check(context, status, "getting ONNX tensor rank")) {
        api->ReleaseTypeInfo(type_info);
        return false;
    }
    if (dim_count > ORT_TENSOR_MAX_RANK) {
        fprintf(stderr, "ONNX tensor rank is too large: %lld\n",
                (int64)dim_count);
        api->ReleaseTypeInfo(type_info);
        return false;
    }

    status = api->GetDimensions(tensor_info, dims, dim_count);
    if (!ort_check(context, status, "getting ONNX tensor shape")) {
        api->ReleaseTypeInfo(type_info);
        return false;
    }

    *shape_len = (int32)dim_count;
    for (int32 i = 0; i < *shape_len; i += 1) {
        shape[i] = (int64)dims[i];
    }

    api->ReleaseTypeInfo(type_info);

    return true;
}

void
ort_context_init_empty(OrtContext *context) {
    context->api = 0;
    context->environment = 0;
    context->memory_info = 0;
    context->allocator = 0;

    return;
}

bool
ort_context_init(OrtContext *context) {
    OrtApi *api;
    OrtStatus *status;

    ort_context_init_empty(context);

    context->api = (void *)OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (context->api == NULL) {
        fprintf(stderr, "ONNX Runtime API is unavailable\n");
        return false;
    }

    api = (OrtApi *)context->api;
    status = api->CreateEnv(
        ORT_LOGGING_LEVEL_WARNING,
        "uvr-c",
        (OrtEnv **)&context->environment
    );
    if (!ort_check(context, status, "creating ONNX Runtime environment")) {
        ort_context_destroy(context);
        return false;
    }

    status = api->CreateCpuMemoryInfo(
        OrtArenaAllocator,
        OrtMemTypeDefault,
        (OrtMemoryInfo **)&context->memory_info
    );
    if (!ort_check(context, status, "creating ONNX Runtime memory info")) {
        ort_context_destroy(context);
        return false;
    }

    status = api->GetAllocatorWithDefaultOptions(
        (OrtAllocator **)&context->allocator
    );
    if (!ort_check(context, status, "getting ONNX Runtime default allocator")) {
        ort_context_destroy(context);
        return false;
    }

    return true;
}

void
ort_context_destroy(OrtContext *context) {
    OrtApi *api;

    api = (OrtApi *)context->api;
    if (api) {
        if (context->memory_info) {
            api->ReleaseMemoryInfo((OrtMemoryInfo *)context->memory_info);
        }
        if (context->environment) {
            api->ReleaseEnv((OrtEnv *)context->environment);
        }
    }

    ort_context_init_empty(context);

    return;
}

void
ort_model_init_empty(OrtModel *model) {
    model->session = 0;

    model->input_name = 0;
    model->output_name = 0;

    model->input_shape_len = 0;
    model->output_shape_len = 0;
    model->input_count = 0;
    model->output_count = 0;

    return;
}

bool
ort_model_load(OrtContext *context, OrtModel *model, char *model_path) {
    OrtApi *api;
    OrtSessionOptions *options;
    OrtStatus *status;

    ort_model_init_empty(model);
    api = (OrtApi *)context->api;
    if ((api == NULL) || (context->environment == NULL)) {
        fprintf(stderr, "ONNX Runtime context is not initialized\n");
        return false;
    }

    options = NULL;
    status = api->CreateSessionOptions(&options);
    if (!ort_check(context, status, "creating ONNX Runtime session options")) {
        return false;
    }

    status = api->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL);
    if (!ort_check(context, status, "configuring ONNX Runtime optimizations")) {
        api->ReleaseSessionOptions(options);
        return false;
    }

    status = api->CreateSession(
        (OrtEnv *)context->environment,
        model_path,
        options,
        (OrtSession **)&model->session
    );
    api->ReleaseSessionOptions(options);
    if (!ort_check(context, status, "loading ONNX model")) {
        ort_model_destroy(context, model);
        return false;
    }

    if (!ort_model_get_io_info(context, model)) {
        ort_model_destroy(context, model);
        return false;
    }

    return true;
}

bool
ort_model_get_io_info(OrtContext *context, OrtModel *model) {
    OrtApi *api;
    OrtStatus *status;
    size_t count;

    api = (OrtApi *)context->api;
    if ((api == NULL) || (model->session == NULL)) {
        fprintf(stderr, "ONNX Runtime model is not loaded\n");
        return false;
    }

    if (model->input_name) {
        status = api->AllocatorFree((OrtAllocator *)context->allocator,
                                    model->input_name);
        if (!ort_check(context, status, "freeing ONNX input name")) {
            return false;
        }
        model->input_name = 0;
    }
    if (model->output_name) {
        status = api->AllocatorFree((OrtAllocator *)context->allocator,
                                    model->output_name);
        if (!ort_check(context, status, "freeing ONNX output name")) {
            return false;
        }
        model->output_name = 0;
    }

    status = api->SessionGetInputCount((OrtSession *)model->session, &count);
    if (!ort_check(context, status, "getting ONNX model input count")) {
        return false;
    }
    if ((count <= 0) || (count > INT_MAX)) {
        fprintf(stderr, "unsupported ONNX input count: %lld\n", (int64)count);
        return false;
    }
    model->input_count = (int32)count;

    status = api->SessionGetOutputCount((OrtSession *)model->session, &count);
    if (!ort_check(context, status, "getting ONNX model output count")) {
        return false;
    }
    if ((count <= 0) || (count > INT_MAX)) {
        fprintf(stderr, "unsupported ONNX output count: %lld\n", (int64)count);
        return false;
    }
    model->output_count = (int32)count;

    status = api->SessionGetInputName(
        (OrtSession *)model->session,
        0,
        (OrtAllocator *)context->allocator,
        &model->input_name
    );
    if (!ort_check(context, status, "getting ONNX input name")) {
        return false;
    }

    status = api->SessionGetOutputName(
        (OrtSession *)model->session,
        0,
        (OrtAllocator *)context->allocator,
        &model->output_name
    );
    if (!ort_check(context, status, "getting ONNX output name")) {
        return false;
    }

    if (!ort_model_read_tensor_info(
            context,
            (OrtSession *)model->session,
            true,
            model->input_shape,
            &model->input_shape_len)) {
        return false;
    }
    if (!ort_model_read_tensor_info(
            context,
            (OrtSession *)model->session,
            false,
            model->output_shape,
            &model->output_shape_len)) {
        return false;
    }

    return true;
}

void
ort_model_destroy(OrtContext *context, OrtModel *model) {
    OrtApi *api;
    OrtStatus *status;

    api = (OrtApi *)context->api;
    if (api) {
        if (model->input_name) {
            status = api->AllocatorFree((OrtAllocator *)context->allocator,
                                        model->input_name);
            ort_check(context, status, "freeing ONNX input name");
        }
        if (model->output_name) {
            status = api->AllocatorFree((OrtAllocator *)context->allocator,
                                        model->output_name);
            ort_check(context, status, "freeing ONNX output name");
        }
        if (model->session) {
            api->ReleaseSession((OrtSession *)model->session);
        }
    }

    ort_model_init_empty(model);

    return;
}

void
ort_tensor_init_empty(OrtTensor *tensor) {
    tensor->value = 0;

    tensor->data = 0;
    tensor->data_len = 0;

    tensor->shape_len = 0;

    return;
}

bool
ort_tensor_create_f32(
    OrtContext *context,
    OrtTensor *tensor,
    float *data,
    int64 data_len,
    int64 *shape,
    int32 shape_len
) {
    OrtApi *api;
    OrtStatus *status;
    int64 shape_count;
    int64_t ort_shape[ORT_TENSOR_MAX_RANK];

    ort_tensor_init_empty(tensor);
    api = (OrtApi *)context->api;
    if ((api == NULL) || (context->memory_info == NULL)) {
        fprintf(stderr, "ONNX Runtime context is not initialized\n");
        return false;
    }
    if ((data == NULL) || (data_len <= 0)) {
        fprintf(stderr, "ONNX tensor data is empty\n");
        return false;
    }
    if ((shape == NULL) || (shape_len <= 0)
        || (shape_len > ORT_TENSOR_MAX_RANK)) {
        fprintf(stderr, "ONNX tensor shape is invalid\n");
        return false;
    }

    shape_count = 1;
    for (int32 i = 0; i < shape_len; i += 1) {
        if (shape[i] <= 0) {
            fprintf(stderr, "ONNX tensor shape dimension is invalid\n");
            return false;
        }
        if (shape_count > data_len/shape[i]) {
            fprintf(stderr, "ONNX tensor shape overflows data length\n");
            return false;
        }

        shape_count *= shape[i];
        ort_shape[i] = (int64_t)shape[i];
        tensor->shape[i] = shape[i];
    }
    if (shape_count != data_len) {
        fprintf(stderr, "ONNX tensor shape does not match data length\n");
        return false;
    }

    status = api->CreateTensorWithDataAsOrtValue(
        (OrtMemoryInfo *)context->memory_info,
        data,
        (size_t)(data_len*SIZEOF(*data)),
        ort_shape,
        (size_t)shape_len,
        ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
        (OrtValue **)&tensor->value
    );
    if (!ort_check(context, status, "creating ONNX float32 tensor")) {
        ort_tensor_init_empty(tensor);
        return false;
    }

    tensor->data = data;
    tensor->data_len = data_len;
    tensor->shape_len = shape_len;

    return true;
}

bool
ort_model_run_f32(
    OrtContext *context,
    OrtModel *model,
    OrtTensor *input,
    OrtTensor *output
) {
    OrtApi *api;
    OrtStatus *status;
    OrtValue *input_value;
    OrtValue *output_value;
    OrtTensorTypeAndShapeInfo *tensor_info;
    ONNXTensorElementDataType element_type;
    int64_t dims[ORT_TENSOR_MAX_RANK];
    size_t dim_count;
    size_t element_count;
    int32 is_tensor;
    char *input_names[1];
    char *output_names[1];

    ort_tensor_init_empty(output);
    api = (OrtApi *)context->api;
    if ((api == NULL) || (model->session == NULL)
        || (input->value == NULL)) {
        fprintf(stderr, "ONNX Runtime run arguments are invalid\n");
        return false;
    }

    input_value = (OrtValue *)input->value;
    output_value = NULL;
    input_names[0] = model->input_name;
    output_names[0] = model->output_name;
    status = api->Run(
        (OrtSession *)model->session,
        NULL,
        (char const * const *)input_names,
        (OrtValue const * const *)&input_value,
        1,
        (char const * const *)output_names,
        1,
        &output_value
    );
    if (!ort_check(context, status, "running ONNX model")) {
        return false;
    }

    status = api->IsTensor(output_value, &is_tensor);
    if (!ort_check(context, status, "checking ONNX output tensor")) {
        api->ReleaseValue(output_value);
        return false;
    }
    if (!is_tensor) {
        fprintf(stderr, "ONNX model output is not a tensor\n");
        api->ReleaseValue(output_value);
        return false;
    }

    status = api->GetTensorMutableData(output_value, (void **)&output->data);
    if (!ort_check(context, status, "getting ONNX output tensor data")) {
        api->ReleaseValue(output_value);
        return false;
    }

    status = api->GetTensorTypeAndShape(output_value, &tensor_info);
    if (!ort_check(context, status, "getting ONNX output tensor shape")) {
        api->ReleaseValue(output_value);
        return false;
    }

    status = api->GetTensorElementType(tensor_info, &element_type);
    if (!ort_check(context, status, "getting ONNX output element type")) {
        api->ReleaseTensorTypeAndShapeInfo(tensor_info);
        api->ReleaseValue(output_value);
        return false;
    }
    if (element_type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        fprintf(stderr, "ONNX model output tensor is not float32\n");
        api->ReleaseTensorTypeAndShapeInfo(tensor_info);
        api->ReleaseValue(output_value);
        return false;
    }

    status = api->GetDimensionsCount(tensor_info, &dim_count);
    if (!ort_check(context, status, "getting ONNX output rank")) {
        api->ReleaseTensorTypeAndShapeInfo(tensor_info);
        api->ReleaseValue(output_value);
        return false;
    }
    if (dim_count > ORT_TENSOR_MAX_RANK) {
        fprintf(stderr, "ONNX output tensor rank is too large: %lld\n",
                (int64)dim_count);
        api->ReleaseTensorTypeAndShapeInfo(tensor_info);
        api->ReleaseValue(output_value);
        return false;
    }

    status = api->GetDimensions(tensor_info, dims, dim_count);
    if (!ort_check(context, status, "getting ONNX output dimensions")) {
        api->ReleaseTensorTypeAndShapeInfo(tensor_info);
        api->ReleaseValue(output_value);
        return false;
    }

    status = api->GetTensorShapeElementCount(tensor_info, &element_count);
    if (!ort_check(context, status, "getting ONNX output element count")) {
        api->ReleaseTensorTypeAndShapeInfo(tensor_info);
        api->ReleaseValue(output_value);
        return false;
    }

    if (element_count > (size_t)INT64_MAX) {
        fprintf(stderr, "ONNX output tensor is too large: %llu\n",
                (uint64)element_count);
        api->ReleaseTensorTypeAndShapeInfo(tensor_info);
        api->ReleaseValue(output_value);
        return false;
    }

    output->value = output_value;
    output->data_len = (int64)element_count;
    output->shape_len = (int32)dim_count;
    for (int32 i = 0; i < output->shape_len; i += 1) {
        output->shape[i] = (int64)dims[i];
    }

    api->ReleaseTensorTypeAndShapeInfo(tensor_info);

    return true;
}

void
ort_tensor_destroy(OrtContext *context, OrtTensor *tensor) {
    OrtApi *api;

    api = (OrtApi *)context->api;
    if (api && tensor->value) {
        api->ReleaseValue((OrtValue *)tensor->value);
    }

    ort_tensor_init_empty(tensor);

    return;
}

#if TESTING_ort

int
main(void) {
    OrtContext context;
    OrtModel model;
    OrtTensor tensor;

    ort_context_init_empty(&context);
    ort_model_init_empty(&model);
    ort_tensor_init_empty(&tensor);

    return 0;
}

#endif /* TESTING_ort */
