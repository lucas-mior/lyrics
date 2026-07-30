#if !defined(MDX_H)
#define MDX_H

#include "../cbase/primitives.h"
#include "audio.h"
#include "ort.h"
#include "stft.h"

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
int64 mdx_input_tensor_len(MdxConfig *config);
bool mdx_pack_input(
    MdxConfig *config,
    StftPlan *stft_plan,
    float *left,
    float *right,
    int64 frame_count,
    float *tensor,
    int64 tensor_len
);
bool mdx_unpack_output(
    MdxConfig *config,
    StftPlan *stft_plan,
    float *tensor,
    int64 tensor_len,
    float *left,
    float *right,
    int64 frame_count
);
bool mdx_process_song(
    MdxConfig *config,
    StftPlan *stft_plan,
    OrtContext *ort_context,
    OrtModel *ort_model,
    AudioBuffer *input,
    AudioBuffer *output
);
void mdx_model_info_init_empty(MdxModelInfo *info);
bool mdx_model_inspect(
    MdxModelInfo *info,
    MdxConfig *config,
    OrtModel *model
);

#endif /* MDX_H */
