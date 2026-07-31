#if !defined(MDX_H)
#define MDX_H

#include "cbase.h"
#include "audio.h"
#include "ort.h"
#include "stft.h"
#include "progress.h"

enum MdxModelOutput {
    MDX_MODEL_OUTPUT_VOCALS,
    MDX_MODEL_OUTPUT_INSTRUMENTAL,
};

enum MdxClipMode {
    MDX_CLIP_MODE_CLAMP,
    MDX_CLIP_MODE_NONE,
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
    enum MdxClipMode clip_mode;
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

static void mdx_config_init(MdxConfig *config);
static bool mdx_config_prepare(MdxConfig *config);
static int64 mdx_input_tensor_len(MdxConfig *config);
static bool mdx_pack_input(
    MdxConfig *config,
    StftPlan *stft_plan,
    float *left,
    float *right,
    int64 frame_count,
    float *tensor,
    int64 tensor_len
);
static bool mdx_unpack_output(
    MdxConfig *config,
    StftPlan *stft_plan,
    float *tensor,
    int64 tensor_len,
    float *left,
    float *right,
    int64 frame_count
);
static bool mdx_process_song(
    MdxConfig *config,
    StftPlan *stft_plan,
    OrtContext *ort_context,
    OrtModel *ort_model,
    AudioBuffer *input,
    AudioBuffer *output
);
static bool mdx_process_song_with_progress(
    MdxConfig *config,
    StftPlan *stft_plan,
    OrtContext *ort_context,
    OrtModel *ort_model,
    AudioBuffer *input,
    AudioBuffer *output,
    bool print_progress
);
static void mdx_model_info_init_empty(MdxModelInfo *info);
static bool mdx_model_inspect(
    MdxModelInfo *info,
    MdxConfig *config,
    OrtModel *model
);

#endif /* MDX_H */
