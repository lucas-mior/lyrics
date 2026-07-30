#include "mdx.h"

void
mdx_config_init(MdxConfig *config) {
    config->sample_rate = 44100;
    config->channel_count = 2;
    config->n_fft = 6144;
    config->hop = 1024;
    config->dim_f = 0;
    config->dim_t = 0;
    config->chunk_seconds = 30;
    config->margin_seconds = 3;

    config->compensate = 1.035f;
    config->denoise = false;

    config->model_output = MDX_MODEL_OUTPUT_VOCALS;

    return;
}

#if TESTING_mdx

int
main(void) {
    MdxConfig config;

    mdx_config_init(&config);

    return 0;
}

#endif /* TESTING_mdx */
