#include "ort.h"

#include <onnxruntime_c_api.h>
#include <stdio.h>

void
ort_context_init_empty(OrtContext *context) {
    context->api = 0;
    context->environment = 0;
    context->memory_info = 0;

    return;
}

static bool
ort_status_ok(OrtApi *api, OrtStatus *status) {
    char *message;

    if (status == 0) {
        return true;
    }

    message = (char *)api->GetErrorMessage(status);
    if (message != 0) {
        fprintf(stderr, "ONNX Runtime error: %s\n", message);
    }
    api->ReleaseStatus(status);

    return false;
}

bool
ort_context_init(OrtContext *context) {
    OrtApi *api;
    OrtStatus *status;

    ort_context_init_empty(context);

    api = (OrtApi *)OrtGetApiBase()->GetApi(ORT_API_VERSION);
    if (api == 0) {
        return false;
    }

    context->api = api;
    status = api->CreateEnv(ORT_LOGGING_LEVEL_WARNING,
                            "uvr-c",
                            (OrtEnv **)&context->environment);
    if (!ort_status_ok(api, status)) {
        ort_context_destroy(context);
        return false;
    }

    status = api->CreateCpuMemoryInfo(OrtArenaAllocator,
                                      OrtMemTypeDefault,
                                      (OrtMemoryInfo **)&context->memory_info);
    if (!ort_status_ok(api, status)) {
        ort_context_destroy(context);
        return false;
    }

    return true;
}

void
ort_context_destroy(OrtContext *context) {
    OrtApi *api;

    api = context->api;
    if (api != 0) {
        if (context->memory_info != 0) {
            api->ReleaseMemoryInfo(context->memory_info);
        }
        if (context->environment != 0) {
            api->ReleaseEnv(context->environment);
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

    return;
}

bool
ort_model_load(OrtContext *context, OrtModel *model, char *model_path) {
    OrtApi *api;
    OrtSessionOptions *session_options;
    OrtStatus *status;

    ort_model_init_empty(model);
    api = context->api;
    if ((api == 0) || (context->environment == 0)) {
        return false;
    }

    session_options = 0;
    status = api->CreateSessionOptions(&session_options);
    if (!ort_status_ok(api, status)) {
        return false;
    }

    status = api->CreateSession(context->environment,
                                (ORTCHAR_T *)model_path,
                                session_options,
                                (OrtSession **)&model->session);
    api->ReleaseSessionOptions(session_options);
    if (!ort_status_ok(api, status)) {
        ort_model_destroy(context, model);
        return false;
    }

    return true;
}

void
ort_model_destroy(OrtContext *context, OrtModel *model) {
    OrtApi *api;

    api = context->api;
    if ((api != 0) && (model->session != 0)) {
        api->ReleaseSession(model->session);
    }

    ort_model_init_empty(model);

    return;
}

#if TESTING_ort

static int32
ort_test_fail(char *name) {
    fprintf(stderr, "ort test failed: %s\n", name);

    return 1;
}

int
main(void) {
    OrtContext context;
    OrtModel model;

    ort_context_init_empty(&context);
    ort_model_init_empty(&model);
    if ((context.api != 0)
        || (context.environment != 0)
        || (context.memory_info != 0)) {
        return ort_test_fail("empty context");
    }
    if (model.session != 0) {
        return ort_test_fail("empty model");
    }

    return 0;
}

#endif /* TESTING_ort */
