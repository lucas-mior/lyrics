#include "app.h"

#include "audio.h"
#include "cli.h"
#include "fftw.h"
#include "mdx.h"
#include "ort.h"

#include <stdio.h>
#include <stdlib.h>

int32
app_run(int argc, char **argv) {
    AudioBuffer input_audio;
    CliOptions options;
    FftwRealPlan fftw_plan;
    MdxConfig mdx_config;
    MdxModelInfo mdx_info;
    OrtContext ort_context;
    OrtModel ort_model;
    FILE *model_file;
    int32 parse_result;
    int32 result;

    cli_options_init(&options);
    parse_result = cli_parse(&options, argc, argv);
    if (parse_result > 0) {
        return EXIT_SUCCESS;
    }
    if (parse_result < 0) {
        cli_print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    result = EXIT_FAILURE;
    audio_buffer_init(&input_audio);
    fftw_real_plan_init_empty(&fftw_plan);
    mdx_config_init(&mdx_config);
    mdx_model_info_init_empty(&mdx_info);
    ort_context_init_empty(&ort_context);
    ort_model_init_empty(&ort_model);

    mdx_config.n_fft = options.n_fft;
    mdx_config.hop = options.hop;
    mdx_config.dim_f = options.dim_f;
    mdx_config.dim_t = options.dim_t;
    mdx_config.chunk_seconds = options.chunk_seconds;
    mdx_config.margin_seconds = options.margin_seconds;
    mdx_config.compensate = options.compensate;
    mdx_config.denoise = options.denoise;
    if (options.model_output == CLI_MODEL_OUTPUT_INSTRUMENTAL) {
        mdx_config.model_output = MDX_MODEL_OUTPUT_INSTRUMENTAL;
    }

    if (!audio_check_ffmpeg(options.ffmpeg_path)) {
        fprintf(stderr, "could not run ffmpeg: %s\n", options.ffmpeg_path);
        fprintf(stderr, "set --ffmpeg to a valid FFmpeg executable\n");
        goto cleanup;
    }

    if (!audio_can_decode_file(options.input_path, options.ffmpeg_path)) {
        fprintf(stderr, "could not decode input with ffmpeg: %s\n",
                options.input_path);
        goto cleanup;
    }

    model_file = fopen(options.model_path, "rb");
    if (model_file == NULL) {
        fprintf(stderr, "could not read ONNX model: %s\n", options.model_path);
        goto cleanup;
    }
    fclose(model_file);

    if (!fftw_real_plan_init(&fftw_plan, mdx_config.n_fft)) {
        fprintf(stderr, "could not initialize FFTW plan for n_fft=%d\n",
                mdx_config.n_fft);
        goto cleanup;
    }

    if (!ort_context_init(&ort_context)) {
        fprintf(stderr, "could not initialize ONNX Runtime\n");
        goto cleanup;
    }

    if (!ort_model_load(&ort_context, &ort_model, options.model_path)) {
        fprintf(stderr, "could not load ONNX model: %s\n",
                options.model_path);
        goto cleanup;
    }

    if (!mdx_model_inspect(&mdx_info, &mdx_config, &ort_model)) {
        fprintf(stderr, "ONNX model is not a supported MDX-Net model: %s\n",
                options.model_path);
        goto cleanup;
    }

    if (!mdx_config_prepare(&mdx_config)) {
        fprintf(stderr, "could not prepare MDX configuration\n");
        goto cleanup;
    }

    if (!audio_read_file(&input_audio,
                         options.input_path,
                         options.ffmpeg_path)) {
        fprintf(stderr, "could not decode input audio: %s\n",
                options.input_path);
        goto cleanup;
    }

    cli_print_options(&options);
    fprintf(stderr,
            "MDX model: input=%s output=%s shape=[%d, %d, %d, %d]\n",
            mdx_info.input_name,
            mdx_info.output_name,
            mdx_info.batch_size,
            mdx_info.channel_count,
            mdx_info.dim_f,
            mdx_info.dim_t);
    fprintf(stderr,
            "MDX config: sample_rate=%d channels=%d dim_c=%d n_fft=%d "
            "hop=%d chunk_size=%d trim=%d gen_size=%d\n",
            mdx_config.sample_rate,
            mdx_config.channel_count,
            mdx_config.dim_c,
            mdx_config.n_fft,
            mdx_config.hop,
            mdx_config.chunk_size,
            mdx_config.trim,
            mdx_config.gen_size);
    fprintf(stderr,
            "decoded audio: sample_rate=%d channels=%d frames=%lld\n",
            input_audio.sample_rate,
            input_audio.channel_count,
            input_audio.frame_count);

    if (!audio_write_file(&input_audio,
                          options.output_path,
                          options.format,
                          options.ffmpeg_path)) {
        fprintf(stderr, "could not write output audio: %s\n",
                options.output_path);
        goto cleanup;
    }

    fprintf(stderr, "wrote decoded audio copy: %s\n", options.output_path);
    fprintf(stderr, "audio extraction is not implemented yet\n");
    result = EXIT_SUCCESS;

cleanup:
    audio_buffer_destroy(&input_audio);
    ort_model_destroy(&ort_context, &ort_model);
    ort_context_destroy(&ort_context);
    fftw_real_plan_destroy(&fftw_plan);

    return result;
}

#if TESTING_app

int
main(void) {
    return 0;
}

#endif /* TESTING_app */
