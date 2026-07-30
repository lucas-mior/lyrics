#include "mdx.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

void
mdx_config_init(MdxConfig *config) {
    config->sample_rate = 44100;
    config->channel_count = 2;
    config->dim_c = 4;
    config->n_fft = 6144;
    config->hop = 1024;
    config->dim_f = 0;
    config->dim_t = 0;
    config->chunk_seconds = 30;
    config->margin_seconds = 3;

    config->chunk_size = 0;
    config->trim = 0;
    config->gen_size = 0;

    config->compensate = 1.035f;
    config->denoise = false;

    config->model_output = MDX_MODEL_OUTPUT_VOCALS;

    return;
}

bool
mdx_config_prepare(MdxConfig *config) {
    int32 max_dim_f;
    int32 chunk_size;
    int32 trim;

    if (config == NULL) {
        fprintf(stderr, "MDX configuration is missing\n");
        return false;
    }
    if (config->sample_rate != 44100) {
        fprintf(stderr, "MDX sample rate must be 44100, got %d\n",
                config->sample_rate);
        return false;
    }
    if (config->channel_count != 2) {
        fprintf(stderr, "MDX audio channel count must be 2, got %d\n",
                config->channel_count);
        return false;
    }
    if (config->dim_c != 4) {
        fprintf(stderr, "MDX spectrogram channel count must be 4, got %d\n",
                config->dim_c);
        return false;
    }
    if (config->n_fft <= 0) {
        fprintf(stderr, "MDX n_fft must be greater than zero\n");
        return false;
    }
    if ((config->n_fft & 1) != 0) {
        fprintf(stderr, "MDX n_fft must be even, got %d\n",
                config->n_fft);
        return false;
    }
    if (config->hop <= 0) {
        fprintf(stderr, "MDX hop must be greater than zero\n");
        return false;
    }
    if (config->hop > config->n_fft) {
        fprintf(stderr, "MDX hop must not exceed n_fft, got %d/%d\n",
                config->hop,
                config->n_fft);
        return false;
    }
    if (config->dim_f <= 0) {
        fprintf(stderr, "MDX dim_f must be known before preparing config\n");
        return false;
    }
    if (config->dim_t <= 1) {
        fprintf(stderr, "MDX dim_t must be greater than 1, got %d\n",
                config->dim_t);
        return false;
    }
    if (config->chunk_seconds <= 0) {
        fprintf(stderr, "MDX chunk seconds must be greater than zero\n");
        return false;
    }
    if (config->margin_seconds < 0) {
        fprintf(stderr, "MDX margin seconds must not be negative\n");
        return false;
    }
    if (config->compensate < 0.0f) {
        fprintf(stderr, "MDX compensate must not be negative\n");
        return false;
    }

    max_dim_f = config->n_fft/2 + 1;
    if (config->dim_f > max_dim_f) {
        fprintf(stderr, "MDX dim_f=%d exceeds STFT bins=%d\n",
                config->dim_f,
                max_dim_f);
        return false;
    }
    if ((config->dim_t - 1) > INT_MAX/config->hop) {
        fprintf(stderr, "MDX chunk size overflows int32\n");
        return false;
    }

    chunk_size = config->hop*(config->dim_t - 1);
    trim = config->n_fft/2;
    if (chunk_size <= 2*trim) {
        fprintf(stderr,
                "MDX gen_size must be positive; got chunk=%d trim=%d\n",
                chunk_size,
                trim);
        return false;
    }

    config->chunk_size = chunk_size;
    config->trim = trim;
    config->gen_size = chunk_size - 2*trim;

    return true;
}


int64
mdx_input_tensor_len(MdxConfig *config) {
    int64 dim_f;
    int64 dim_t;
    int64 dim_c;
    int64 freq_time;

    if (config == 0) {
        return -1;
    }
    if (config->dim_f <= 0 || config->dim_t <= 0 || config->dim_c <= 0) {
        return -1;
    }

    dim_f = (int64)config->dim_f;
    dim_t = (int64)config->dim_t;
    dim_c = (int64)config->dim_c;
    if (dim_f > INT64_MAX/dim_t) {
        return -1;
    }
    freq_time = dim_f*dim_t;
    if (dim_c > INT64_MAX/freq_time) {
        return -1;
    }

    return dim_c*freq_time;
}

bool
mdx_pack_input(
    MdxConfig *config,
    StftPlan *stft_plan,
    float *left,
    float *right,
    int64 frame_count,
    float *tensor,
    int64 tensor_len
) {
    float *left_real;
    float *left_imag;
    float *right_real;
    float *right_imag;
    int64 full_len;
    int64 wanted_len;
    int64 channel_stride;
    int32 stft_frames;
    int32 complex_count;

    if (config == 0 || stft_plan == 0 || left == 0 || right == 0
        || tensor == 0) {
        return false;
    }
    if (config->dim_c != 4 || config->dim_f <= 0 || config->dim_t <= 0) {
        return false;
    }
    if (stft_plan->n_fft != config->n_fft || stft_plan->hop != config->hop) {
        return false;
    }
    if (stft_plan->complex_count < config->dim_f) {
        return false;
    }
    if (config->chunk_size <= 0 || frame_count != config->chunk_size) {
        return false;
    }

    wanted_len = mdx_input_tensor_len(config);
    if (wanted_len <= 0 || tensor_len < wanted_len) {
        return false;
    }

    stft_frames = stft_frame_count(stft_plan, frame_count);
    if (stft_frames != config->dim_t) {
        return false;
    }

    complex_count = stft_plan->complex_count;
    if ((int64)complex_count > INT64_MAX/(int64)stft_frames) {
        return false;
    }
    full_len = (int64)complex_count*(int64)stft_frames;
    if (full_len > INT64_MAX/(int64)sizeof(*left_real)) {
        return false;
    }

    left_real = malloc((size_t)(full_len*sizeof(*left_real)));
    left_imag = malloc((size_t)(full_len*sizeof(*left_imag)));
    right_real = malloc((size_t)(full_len*sizeof(*right_real)));
    right_imag = malloc((size_t)(full_len*sizeof(*right_imag)));
    if (left_real == 0 || left_imag == 0 || right_real == 0
        || right_imag == 0) {
        free(left_real);
        free(left_imag);
        free(right_real);
        free(right_imag);
        return false;
    }

    if (!stft_forward_channel(stft_plan,
                              left,
                              frame_count,
                              left_real,
                              left_imag,
                              stft_frames)) {
        free(left_real);
        free(left_imag);
        free(right_real);
        free(right_imag);
        return false;
    }
    if (!stft_forward_channel(stft_plan,
                              right,
                              frame_count,
                              right_real,
                              right_imag,
                              stft_frames)) {
        free(left_real);
        free(left_imag);
        free(right_real);
        free(right_imag);
        return false;
    }

    channel_stride = (int64)config->dim_f*(int64)config->dim_t;
    for (int32 bin = 0; bin < config->dim_f; bin += 1) {
        for (int32 frame = 0; frame < config->dim_t; frame += 1) {
            int64 input_index;
            int64 output_index;

            input_index = (int64)bin*(int64)stft_frames + (int64)frame;
            output_index = (int64)bin*(int64)config->dim_t
                           + (int64)frame;

            tensor[output_index] = left_real[input_index];
            tensor[channel_stride + output_index] = left_imag[input_index];
            tensor[2*channel_stride + output_index] = right_real[input_index];
            tensor[3*channel_stride + output_index] = right_imag[input_index];
        }
    }

    free(left_real);
    free(left_imag);
    free(right_real);
    free(right_imag);

    return true;
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
mdx_unpack_output(
    MdxConfig *config,
    StftPlan *stft_plan,
    float *tensor,
    int64 tensor_len,
    float *left,
    float *right,
    int64 frame_count
) {
    float *left_real;
    float *left_imag;
    float *right_real;
    float *right_imag;
    int64 full_len;
    int64 wanted_len;
    int64 channel_stride;
    int32 stft_frames;
    int32 complex_count;

    if (config == NULL || stft_plan == NULL || tensor == NULL || left == NULL
        || right == NULL) {
        return false;
    }
    if (config->dim_c != 4 || config->dim_f <= 0 || config->dim_t <= 0) {
        return false;
    }
    if (stft_plan->n_fft != config->n_fft || stft_plan->hop != config->hop) {
        return false;
    }
    if (stft_plan->complex_count < config->dim_f) {
        return false;
    }
    if (config->chunk_size <= 0 || frame_count != config->chunk_size) {
        return false;
    }

    wanted_len = mdx_input_tensor_len(config);
    if (wanted_len <= 0 || tensor_len < wanted_len) {
        return false;
    }

    stft_frames = stft_frame_count(stft_plan, frame_count);
    if (stft_frames != config->dim_t) {
        return false;
    }

    complex_count = stft_plan->complex_count;
    if ((int64)complex_count > INT64_MAX/(int64)stft_frames) {
        return false;
    }
    full_len = (int64)complex_count*(int64)stft_frames;
    if (full_len > INT64_MAX/(int64)sizeof(*left_real)) {
        return false;
    }

    left_real = malloc((size_t)(full_len*sizeof(*left_real)));
    left_imag = malloc((size_t)(full_len*sizeof(*left_imag)));
    right_real = malloc((size_t)(full_len*sizeof(*right_real)));
    right_imag = malloc((size_t)(full_len*sizeof(*right_imag)));
    if (left_real == NULL || left_imag == NULL || right_real == NULL
        || right_imag == NULL) {
        free(left_real);
        free(left_imag);
        free(right_real);
        free(right_imag);
        return false;
    }

    for (int64 i = 0; i < full_len; i += 1) {
        left_real[i] = 0.0f;
        left_imag[i] = 0.0f;
        right_real[i] = 0.0f;
        right_imag[i] = 0.0f;
    }

    channel_stride = (int64)config->dim_f*(int64)config->dim_t;
    for (int32 bin = 0; bin < config->dim_f; bin += 1) {
        for (int32 frame = 0; frame < config->dim_t; frame += 1) {
            int64 input_index;
            int64 output_index;

            input_index = (int64)bin*(int64)config->dim_t + (int64)frame;
            output_index = (int64)bin*(int64)stft_frames + (int64)frame;

            left_real[output_index] = tensor[input_index];
            left_imag[output_index] = tensor[channel_stride + input_index];
            right_real[output_index] = tensor[2*channel_stride + input_index];
            right_imag[output_index] = tensor[3*channel_stride + input_index];
        }
    }

    if (!stft_inverse_channel(stft_plan,
                              left_real,
                              left_imag,
                              stft_frames,
                              left,
                              frame_count)) {
        free(left_real);
        free(left_imag);
        free(right_real);
        free(right_imag);
        return false;
    }
    if (!stft_inverse_channel(stft_plan,
                              right_real,
                              right_imag,
                              stft_frames,
                              right,
                              frame_count)) {
        free(left_real);
        free(left_imag);
        free(right_real);
        free(right_imag);
        return false;
    }

    free(left_real);
    free(left_imag);
    free(right_real);
    free(right_imag);

    return true;
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
    if (input_channels > 0 && input_channels != config->dim_c) {
        fprintf(stderr, "MDX model input channels must be %d, got %lld\n",
                config->dim_c,
                input_channels);
        return false;
    }
    if (output_channels > 0 && output_channels != config->dim_c) {
        fprintf(stderr, "MDX model output channels must be %d, got %lld\n",
                config->dim_c,
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
    info->channel_count = config->dim_c;
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

static bool
mdx_float_close(float a, float b) {
    return fabsf(a - b) < 0.001f;
}

int
main(void) {
    MdxConfig config;
    MdxModelInfo info;
    OrtModel model;
    StftPlan stft_plan;
    float left[12];
    float right[12];
    float tensor[80];
    float too_small[79];
    float left_real[20];
    float left_imag[20];
    float right_real[20];
    float right_imag[20];
    float unpack_left[12];
    float unpack_right[12];
    int64 channel_stride;

    mdx_config_init(&config);
    MDX_TEST_CHECK(config.sample_rate == 44100, "sample rate");
    MDX_TEST_CHECK(config.channel_count == 2, "channel count");
    MDX_TEST_CHECK(config.dim_c == 4, "dim_c");
    MDX_TEST_CHECK(config.n_fft == 6144, "n_fft");
    MDX_TEST_CHECK(config.hop == 1024, "hop");
    MDX_TEST_CHECK(config.chunk_size == 0, "empty chunk size");
    MDX_TEST_CHECK(config.trim == 0, "empty trim");
    MDX_TEST_CHECK(config.gen_size == 0, "empty gen_size");

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
    MDX_TEST_CHECK(mdx_config_prepare(&config), "prepare config");
    MDX_TEST_CHECK(config.chunk_size == 261120, "chunk size");
    MDX_TEST_CHECK(config.trim == 3072, "trim");
    MDX_TEST_CHECK(config.gen_size == 254976, "gen_size");

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

    mdx_config_init(&config);
    config.dim_f = 3074;
    config.dim_t = 256;
    MDX_TEST_CHECK(!mdx_config_prepare(&config), "reject too many bins");

    mdx_config_init(&config);
    config.dim_f = 3072;
    config.dim_t = 4;
    MDX_TEST_CHECK(!mdx_config_prepare(&config), "reject small dim_t");

    mdx_config_init(&config);
    MDX_TEST_CHECK(mdx_input_tensor_len(&config) < 0,
                   "empty tensor len rejected");
    config.n_fft = 8;
    config.hop = 4;
    config.dim_f = 3;
    config.dim_t = 4;
    MDX_TEST_CHECK(mdx_config_prepare(&config), "small pack config");
    MDX_TEST_CHECK(mdx_input_tensor_len(&config) == 48, "tensor len");

    stft_plan_init_empty(&stft_plan);
    MDX_TEST_CHECK(stft_plan_init(&stft_plan, config.n_fft, config.hop),
                   "pack stft plan");
    for (int32 i = 0; i < config.chunk_size; i += 1) {
        left[i] = (float)(i + 1);
        right[i] = (float)(config.chunk_size - i);
    }
    MDX_TEST_CHECK(!mdx_pack_input(&config,
                                   &stft_plan,
                                   left,
                                   right,
                                   config.chunk_size - 1,
                                   tensor,
                                   mdx_input_tensor_len(&config)),
                   "reject short frame count");
    MDX_TEST_CHECK(!mdx_pack_input(&config,
                                   &stft_plan,
                                   left,
                                   right,
                                   config.chunk_size,
                                   too_small,
                                   mdx_input_tensor_len(&config) - 1),
                   "reject short tensor");
    MDX_TEST_CHECK(mdx_pack_input(&config,
                                  &stft_plan,
                                  left,
                                  right,
                                  config.chunk_size,
                                  tensor,
                                  mdx_input_tensor_len(&config)),
                   "pack input");
    MDX_TEST_CHECK(stft_forward_channel(&stft_plan,
                                        left,
                                        config.chunk_size,
                                        left_real,
                                        left_imag,
                                        config.dim_t),
                   "expected left stft");
    MDX_TEST_CHECK(stft_forward_channel(&stft_plan,
                                        right,
                                        config.chunk_size,
                                        right_real,
                                        right_imag,
                                        config.dim_t),
                   "expected right stft");

    channel_stride = (int64)config.dim_f*(int64)config.dim_t;
    for (int32 bin = 0; bin < config.dim_f; bin += 1) {
        for (int32 frame = 0; frame < config.dim_t; frame += 1) {
            int64 input_index;
            int64 output_index;

            input_index = (int64)bin*(int64)config.dim_t + (int64)frame;
            output_index = (int64)bin*(int64)config.dim_t + (int64)frame;
            MDX_TEST_CHECK(mdx_float_close(tensor[output_index],
                                           left_real[input_index]),
                           "left real tensor channel");
            MDX_TEST_CHECK(mdx_float_close(tensor[channel_stride
                                                 + output_index],
                                           left_imag[input_index]),
                           "left imag tensor channel");
            MDX_TEST_CHECK(mdx_float_close(tensor[2*channel_stride
                                                 + output_index],
                                           right_real[input_index]),
                           "right real tensor channel");
            MDX_TEST_CHECK(mdx_float_close(tensor[3*channel_stride
                                                 + output_index],
                                           right_imag[input_index]),
                           "right imag tensor channel");
        }
    }

    for (int64 i = 0; i < mdx_input_tensor_len(&config); i += 1) {
        tensor[i] = 0.0f;
    }
    MDX_TEST_CHECK(!mdx_unpack_output(&config,
                                      &stft_plan,
                                      tensor,
                                      mdx_input_tensor_len(&config),
                                      unpack_left,
                                      unpack_right,
                                      config.chunk_size - 1),
                   "reject short unpack frame count");
    MDX_TEST_CHECK(!mdx_unpack_output(&config,
                                      &stft_plan,
                                      too_small,
                                      mdx_input_tensor_len(&config) - 1,
                                      unpack_left,
                                      unpack_right,
                                      config.chunk_size),
                   "reject short unpack tensor");
    MDX_TEST_CHECK(mdx_unpack_output(&config,
                                     &stft_plan,
                                     tensor,
                                     mdx_input_tensor_len(&config),
                                     unpack_left,
                                     unpack_right,
                                     config.chunk_size),
                   "unpack zero output");
    for (int32 i = 0; i < config.chunk_size; i += 1) {
        MDX_TEST_CHECK(mdx_float_close(unpack_left[i], 0.0f),
                       "zero left unpack");
        MDX_TEST_CHECK(mdx_float_close(unpack_right[i], 0.0f),
                       "zero right unpack");
    }

    stft_plan_destroy(&stft_plan);
    mdx_config_init(&config);
    config.n_fft = 8;
    config.hop = 4;
    config.dim_f = 5;
    config.dim_t = 4;
    MDX_TEST_CHECK(mdx_config_prepare(&config), "full-bin unpack config");
    MDX_TEST_CHECK(stft_plan_init(&stft_plan, config.n_fft, config.hop),
                   "full-bin stft plan");
    for (int32 i = 0; i < config.chunk_size; i += 1) {
        left[i] = (float)i/8.0f - 0.5f;
        right[i] = 0.25f - (float)i/16.0f;
    }
    MDX_TEST_CHECK(mdx_pack_input(&config,
                                  &stft_plan,
                                  left,
                                  right,
                                  config.chunk_size,
                                  tensor,
                                  mdx_input_tensor_len(&config)),
                   "pack full-bin input");
    MDX_TEST_CHECK(mdx_unpack_output(&config,
                                     &stft_plan,
                                     tensor,
                                     mdx_input_tensor_len(&config),
                                     unpack_left,
                                     unpack_right,
                                     config.chunk_size),
                   "unpack full-bin output");
    for (int32 i = 0; i < config.chunk_size; i += 1) {
        MDX_TEST_CHECK(mdx_float_close(unpack_left[i], left[i]),
                       "full-bin left roundtrip");
        MDX_TEST_CHECK(mdx_float_close(unpack_right[i], right[i]),
                       "full-bin right roundtrip");
    }

    stft_plan_destroy(&stft_plan);

    return 0;
}

#endif /* TESTING_mdx */
