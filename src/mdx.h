#if !defined(MDX_H)
#define MDX_H

#include "../cbase/primitives.h"
#include "ort.h"

#include <stdbool.h>

enum MdxModelOutput {
    MDX_MODEL_OUTPUT_VOCALS,
    MDX_MODEL_OUTPUT_INSTRUMENTAL,
};

typedef struct MdxConfig {
    int32 sample_rate;
    int32 channel_count;
    int32 dim_c;
    int32 n_fft;
    int32 hop;
    int32 dim_f;
    int32 dim_t;
    int32 chunk_seconds;
    int32 margin_seconds;

    int32 chunk_size;
    int32 trim;
    int32 gen_size;

    float compensate;
    bool denoise;

    enum MdxModelOutput model_output;
} MdxConfig;

typedef struct MdxModelInfo {
    char *input_name;
    char *output_name;

    int32 batch_size;
    int32 channel_count;
    int32 dim_f;
    int32 dim_t;

    bool input_shape_dynamic;
    bool output_shape_dynamic;
} MdxModelInfo;

void mdx_config_init(MdxConfig *config);
bool mdx_config_prepare(MdxConfig *config);
void mdx_model_info_init_empty(MdxModelInfo *info);
bool mdx_model_inspect(
    MdxModelInfo *info,
    MdxConfig *config,
    OrtModel *model
);

#endif /* MDX_H */
