#if !defined(MDX_H)
#define MDX_H

#include "../cbase/primitives.h"

#include <stdbool.h>

enum MdxModelOutput {
    MDX_MODEL_OUTPUT_VOCALS,
    MDX_MODEL_OUTPUT_INSTRUMENTAL,
};

typedef struct MdxConfig {
    int32 sample_rate;
    int32 channel_count;
    int32 n_fft;
    int32 hop;
    int32 dim_f;
    int32 dim_t;
    int32 chunk_seconds;
    int32 margin_seconds;

    float compensate;
    bool denoise;

    enum MdxModelOutput model_output;
} MdxConfig;

void mdx_config_init(MdxConfig *config);

#endif /* MDX_H */
