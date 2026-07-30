#include "ort.h"

void
ort_context_init_empty(OrtContext *context) {
    context->environment = 0;
    context->memory_info = 0;

    return;
}

bool
ort_context_init(OrtContext *context) {
    ort_context_init_empty(context);

    return false;
}

void
ort_context_destroy(OrtContext *context) {
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
    (void)context;
    (void)model_path;

    ort_model_init_empty(model);

    return false;
}

void
ort_model_destroy(OrtContext *context, OrtModel *model) {
    (void)context;

    ort_model_init_empty(model);

    return;
}

#if TESTING_ort

int
main(void) {
    OrtContext context;
    OrtModel model;

    ort_context_init_empty(&context);
    ort_model_init_empty(&model);

    return 0;
}

#endif /* TESTING_ort */
