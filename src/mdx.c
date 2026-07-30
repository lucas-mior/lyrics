#include "mdx.h"

#include <limits.h>
#include <stdio.h>

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

void
mdx_model_info_init_empty(MdxModelInfo *info) {
    info->input_name = 0;
    info->output_name = 0;

    info->batch_size = 0;
    info->channel_count = 0;
    info->dim_f = 0;
    info->dim_t = 0;

    info->input_shape_dynamic = false;
    info->output_shape_dynamic = false;

    return;
}

bool
mdx_model_inspect(MdxModelInfo *info, MdxConfig *config, OrtModel *model) {
    int64 input_batch;
    int64 input_channels;
    int64 input_dim_f;
    int64 input_dim_t;
    int64 output_batch;
    int64 output_channels;
    int64 output_dim_f;
    int64 output_dim_t;

    mdx_model_info_init_empty(info);
    if (config == 0 || model == 0) {
        fprintf(stderr, "MDX model inspection arguments are invalid\n");
        return false;
    }
    if (model->input_name == 0 || model->output_name == 0) {
        fprintf(stderr, "ONNX model input/output names are missing\n");
        return false;
    }
    if (model->input_count != 1 || model->output_count != 1) {
        fprintf(stderr,
                "MDX models must have 1 input and 1 output, got %d/%d\n",
                model->input_count,
                model->output_count);
        return false;
    }
    if (model->input_shape_len != 4) {
        fprintf(stderr, "MDX model input rank must be 4, got %d\n",
                model->input_shape_len);
        return false;
    }
    if (model->output_shape_len != 4) {
        fprintf(stderr, "MDX model output rank must be 4, got %d\n",
                model->output_shape_len);
        return false;
    }

    input_batch = model->input_shape[0];
    input_channels = model->input_shape[1];
    input_dim_f = model->input_shape[2];
    input_dim_t = model->input_shape[3];
    output_batch = model->output_shape[0];
    output_channels = model->output_shape[1];
    output_dim_f = model->output_shape[2];
    output_dim_t = model->output_shape[3];

    if (input_batch > 0 && input_batch != 1) {
        fprintf(stderr, "MDX model input batch must be 1, got %lld\n",
                input_batch);
        return false;
    }
    if (output_batch > 0 && output_batch != 1) {
        fprintf(stderr, "MDX model output batch must be 1, got %lld\n",
                output_batch);
        return false;
    }
    if (input_channels > 0 && input_channels != 4) {
        fprintf(stderr, "MDX model input channels must be 4, got %lld\n",
                input_channels);
        return false;
    }
    if (output_channels > 0 && output_channels != 4) {
        fprintf(stderr, "MDX model output channels must be 4, got %lld\n",
                output_channels);
        return false;
    }

    if (input_dim_f > INT_MAX || input_dim_t > INT_MAX) {
        fprintf(stderr, "MDX model input dimensions are too large\n");
        return false;
    }
    if (output_dim_f > INT_MAX || output_dim_t > INT_MAX) {
        fprintf(stderr, "MDX model output dimensions are too large\n");
        return false;
    }

    if (input_dim_f > 0) {
        if (config->dim_f > 0 && config->dim_f != input_dim_f) {
            fprintf(stderr, "--dim-f=%d does not match model dim_f=%lld\n",
                    config->dim_f,
                    input_dim_f);
            return false;
        }
        config->dim_f = (int32)input_dim_f;
    }
    if (input_dim_t > 0) {
        if (config->dim_t > 0 && config->dim_t != input_dim_t) {
            fprintf(stderr, "--dim-t=%d does not match model dim_t=%lld\n",
                    config->dim_t,
                    input_dim_t);
            return false;
        }
        config->dim_t = (int32)input_dim_t;
    }
    if (output_dim_f > 0) {
        if (config->dim_f > 0 && config->dim_f != output_dim_f) {
            fprintf(stderr,
                    "MDX output dim_f=%lld does not match dim_f=%d\n",
                    output_dim_f,
                    config->dim_f);
            return false;
        }
        config->dim_f = (int32)output_dim_f;
    }
    if (output_dim_t > 0) {
        if (config->dim_t > 0 && config->dim_t != output_dim_t) {
            fprintf(stderr,
                    "MDX output dim_t=%lld does not match dim_t=%d\n",
                    output_dim_t,
                    config->dim_t);
            return false;
        }
        config->dim_t = (int32)output_dim_t;
    }
    if (config->dim_f <= 0) {
        fprintf(stderr, "MDX model has dynamic dim_f; pass --dim-f\n");
        return false;
    }
    if (config->dim_t <= 0) {
        fprintf(stderr, "MDX model has dynamic dim_t; pass --dim-t\n");
        return false;
    }

    info->input_name = model->input_name;
    info->output_name = model->output_name;
    info->batch_size = 1;
    info->channel_count = 4;
    info->dim_f = config->dim_f;
    info->dim_t = config->dim_t;
    info->input_shape_dynamic = input_batch <= 0
                                || input_channels <= 0
                                || input_dim_f <= 0
                                || input_dim_t <= 0;
    info->output_shape_dynamic = output_batch <= 0
                                 || output_channels <= 0
                                 || output_dim_f <= 0
                                 || output_dim_t <= 0;

    return true;
}

#if TESTING_mdx

#define MDX_TEST_CHECK(condition, name)                                      \
    do {                                                                    \
        if (!(condition)) {                                                  \
            fprintf(stderr, "mdx test failed: %s\n", name);                 \
            return 1;                                                       \
        }                                                                   \
    } while (0)

int
main(void) {
    MdxConfig config;
    MdxModelInfo info;
    OrtModel model;

    mdx_config_init(&config);
    MDX_TEST_CHECK(config.sample_rate == 44100, "sample rate");
    MDX_TEST_CHECK(config.channel_count == 2, "channel count");
    MDX_TEST_CHECK(config.n_fft == 6144, "n_fft");
    MDX_TEST_CHECK(config.hop == 1024, "hop");

    ort_model_init_empty(&model);
    model.input_name = "input";
    model.output_name = "output";
    model.input_count = 1;
    model.output_count = 1;
    model.input_shape_len = 4;
    model.output_shape_len = 4;
    model.input_shape[0] = 1;
    model.input_shape[1] = 4;
    model.input_shape[2] = 3072;
    model.input_shape[3] = 256;
    model.output_shape[0] = 1;
    model.output_shape[1] = 4;
    model.output_shape[2] = 3072;
    model.output_shape[3] = 256;

    MDX_TEST_CHECK(mdx_model_inspect(&info, &config, &model), "inspect");
    MDX_TEST_CHECK(config.dim_f == 3072, "derived dim_f");
    MDX_TEST_CHECK(config.dim_t == 256, "derived dim_t");
    MDX_TEST_CHECK(info.input_name == model.input_name, "input name");
    MDX_TEST_CHECK(info.output_name == model.output_name, "output name");
    MDX_TEST_CHECK(info.input_shape_dynamic == false, "static input");
    MDX_TEST_CHECK(info.output_shape_dynamic == false, "static output");

    config.dim_f = 2048;
    MDX_TEST_CHECK(!mdx_model_inspect(&info, &config, &model),
                   "dim_f mismatch");

    mdx_config_init(&config);
    model.input_shape[2] = -1;
    MDX_TEST_CHECK(mdx_model_inspect(&info, &config, &model),
                   "derive dim_f from output");
    MDX_TEST_CHECK(config.dim_f == 3072, "output dim_f");
    MDX_TEST_CHECK(info.input_shape_dynamic == true, "dynamic input");

    mdx_config_init(&config);
    model.output_shape[2] = -1;
    MDX_TEST_CHECK(!mdx_model_inspect(&info, &config, &model),
                   "dynamic dim_f missing override");
    config.dim_f = 3072;
    MDX_TEST_CHECK(mdx_model_inspect(&info, &config, &model),
                   "dynamic dim_f override");

    model.input_shape_len = 2;
    MDX_TEST_CHECK(!mdx_model_inspect(&info, &config, &model),
                   "reject non-mdx rank");

    return 0;
}

#endif /* TESTING_mdx */
