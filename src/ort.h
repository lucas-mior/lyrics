#if !defined(ORT_H)
#define ORT_H

#include "../cbase/primitives.h"

#include <stdbool.h>

typedef struct OrtContext {
    void *environment;
    void *memory_info;
} OrtContext;

typedef struct OrtModel {
    void *session;

    char *input_name;
    char *output_name;
} OrtModel;

void ort_context_init_empty(OrtContext *context);
bool ort_context_init(OrtContext *context);
void ort_context_destroy(OrtContext *context);

void ort_model_init_empty(OrtModel *model);
bool ort_model_load(OrtContext *context, OrtModel *model, char *model_path);
void ort_model_destroy(OrtContext *context, OrtModel *model);

#endif /* ORT_H */
