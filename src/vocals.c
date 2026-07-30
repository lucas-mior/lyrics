#include "cbase.h"
#include "vocals.h"
#include "ort.h"
#include "stft.h"

#if !defined(TESTING_vocals)
#define TESTING_vocals 0
#endif

static void
vocals_extraction_config_init(VocalsExtractionConfig *config) {
    config->model_path = NULL;
    config->ffmpeg_path = "ffmpeg";
    config->print_info = true;

    mdx_config_init(&config->mdx_config);

    return;
}

static void
vocals_print_model_info(MdxModelInfo *info, MdxConfig *config) {
    error2(
        "MDX model: input=%s output=%s shape=[%d, %d, %d, %d]\n",
        info->input_name,
        info->output_name,
        info->batch_size,
        info->channel_count,
        info->dim_f,
        info->dim_t);
    error2(
        "MDX config: sample_rate=%d channels=%d dim_c=%d n_fft=%d "
        "hop=%d chunk_size=%d trim=%d gen_size=%d\n",
        config->sample_rate,
        config->channel_count,
        config->dim_c,
        config->n_fft,
        config->hop,
        config->chunk_size,
        config->trim,
        config->gen_size);

    return;
}

static bool
vocals_config_valid(VocalsExtractionConfig *config) {
    if (config == NULL) {
        error2("vocals extraction configuration is missing\n");
        return false;
    }
    if (config->model_path == NULL) {
        error2("vocals extraction model path is missing\n");
        return false;
    }
    if (config->ffmpeg_path == NULL) {
        error2("FFmpeg executable path is missing\n");
        return false;
    }

    return true;
}

static bool
vocals_prepare_runtime(
    VocalsExtractionConfig *config,
    char *input_path,
    MdxConfig *mdx_config,
    StftPlan *stft_plan,
    MdxModelInfo *mdx_info,
    OrtContext *ort_context,
    OrtModel *ort_model
) {
    *mdx_config = config->mdx_config;

    if (!audio_check_ffmpeg(config->ffmpeg_path)) {
        error2("could not run ffmpeg: %s\n", config->ffmpeg_path);
        error2("set --ffmpeg to a valid FFmpeg executable\n");
        return false;
    }

    if (!audio_can_decode_file(input_path, config->ffmpeg_path)) {
        error2("could not decode input with ffmpeg: %s\n", input_path);
        return false;
    }

    if (!util_file_exists(config->model_path)) {
        error2("could not read ONNX model: %s\n", config->model_path);
        return false;
    }

    if (!stft_plan_init(stft_plan, mdx_config->n_fft, mdx_config->hop)) {
        error2(
            "could not initialize STFT plan for n_fft=%d hop=%d\n",
            mdx_config->n_fft,
            mdx_config->hop);
        return false;
    }

    if (!ort_context_init(ort_context)) {
        error2("could not initialize ONNX Runtime\n");
        return false;
    }

    if (!ort_model_load(ort_context, ort_model, config->model_path)) {
        error2("could not load ONNX model: %s\n", config->model_path);
        return false;
    }

    if (!mdx_model_inspect(mdx_info, mdx_config, ort_model)) {
        error2("ONNX model is not a supported MDX-Net model: %s\n",
                config->model_path);
        return false;
    }

    if (!mdx_config_prepare(mdx_config)) {
        error2("could not prepare MDX configuration\n");
        return false;
    }

    return true;
}

static bool
vocals_read_input_audio(
    AudioBuffer *input_audio,
    char *input_path,
    MdxConfig *mdx_config,
    char *ffmpeg_path
) {
    AudioIoFormat input_format;

    audio_io_format_init(&input_format);
    input_format.sample_rate = mdx_config->sample_rate;
    input_format.channel_count = mdx_config->channel_count;

    if (!audio_read_file_format(input_audio,
                                input_path,
                                &input_format,
                                ffmpeg_path)) {
        error2("could not decode input audio: %s\n", input_path);
        return false;
    }

    error2(
        "decoded audio: sample_rate=%d channels=%d frames=%lld\n",
        input_audio->sample_rate,
        input_audio->channel_count,
        input_audio->frame_count);

    return true;
}

static bool
vocals_extract_audio(
    AudioBuffer *output_audio,
    char *input_path,
    VocalsExtractionConfig *config
) {
    AudioBuffer input_audio;
    MdxConfig mdx_config;
    MdxModelInfo mdx_info;
    OrtContext ort_context;
    OrtModel ort_model;
    StftPlan stft_plan;
    bool result;

    if ((output_audio == NULL) || (input_path == NULL)) {
        return false;
    }
    if (!vocals_config_valid(config)) {
        return false;
    }

    result = false;
    audio_buffer_init(&input_audio);
    stft_plan_init_empty(&stft_plan);
    mdx_config_init(&mdx_config);
    mdx_model_info_init_empty(&mdx_info);
    ort_context_init_empty(&ort_context);
    ort_model_init_empty(&ort_model);

    if (!vocals_prepare_runtime(config,
                                input_path,
                                &mdx_config,
                                &stft_plan,
                                &mdx_info,
                                &ort_context,
                                &ort_model)) {
        goto cleanup;
    }

    if (config->print_info) {
        vocals_print_model_info(&mdx_info, &mdx_config);
    }

    if (!vocals_read_input_audio(&input_audio,
                                 input_path,
                                 &mdx_config,
                                 config->ffmpeg_path)) {
        goto cleanup;
    }

    if (!mdx_process_song(&mdx_config,
                          &stft_plan,
                          &ort_context,
                          &ort_model,
                          &input_audio,
                          output_audio)) {
        error2("could not process audio through MDX model\n");
        goto cleanup;
    }

    result = true;

cleanup:
    audio_buffer_destroy(&input_audio);
    ort_model_destroy(&ort_context, &ort_model);
    ort_context_destroy(&ort_context);
    stft_plan_destroy(&stft_plan);

    return result;
}

static bool
vocals_extract_file(
    VocalsExtractionConfig *config,
    char *input_path,
    char *output_path,
    char *container_format
) {
    AudioBuffer output_audio;
    bool result;

    if ((config == NULL) || (input_path == NULL) || (output_path == NULL)
        || (container_format == NULL)) {
        return false;
    }

    result = false;
    audio_buffer_init(&output_audio);

    if (!vocals_extract_audio(&output_audio, input_path, config)) {
        goto cleanup;
    }

    if (!audio_write_file(&output_audio,
                          output_path,
                          container_format,
                          config->ffmpeg_path)) {
        error2("could not write output audio: %s\n", output_path);
        goto cleanup;
    }

    error2("wrote extracted vocals: %s\n", output_path);
    result = true;

cleanup:
    audio_buffer_destroy(&output_audio);

    return result;
}

#if TESTING_vocals

#define CBASE_IMPLEMENT
#include "cbase.h"

#include "fftw.c"
#include "stft.c"
#include "audio.c"
#include "ort.c"
#include "mdx.c"

int32
main(void) {
    VocalsExtractionConfig config;

    vocals_extraction_config_init(&config);
    ASSERT(config.model_path == NULL);
    ASSERT(strequal(config.ffmpeg_path, "ffmpeg"));
    ASSERT(config.print_info);
    ASSERT(config.mdx_config.sample_rate == 44100);
    ASSERT(config.mdx_config.channel_count == 2);

    return 0;
}

#endif
