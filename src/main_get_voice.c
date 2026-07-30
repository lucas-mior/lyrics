#if !defined(TESTING_app)
#define TESTING_app 0
#endif
#if !defined(TESTING_audio)
#define TESTING_audio 0
#endif
#if !defined(TESTING_cli)
#define TESTING_cli 0
#endif
#if !defined(TESTING_fftw)
#define TESTING_fftw 0
#endif
#if !defined(TESTING_mdx)
#define TESTING_mdx 0
#endif
#if !defined(TESTING_ort)
#define TESTING_ort 0
#endif
#if !defined(TESTING_stft)
#define TESTING_stft 0
#endif

#define CBASE_API_DECL static
#define CBASE_API_DEF static
#define CBASE_IMPLEMENT
#include "cbase.h"

#include "audio.h"
#include "cli.h"
#include "mdx.h"
#include "ort.h"

#include "fftw.c"
#include "stft.c"
#include "audio.c"
#include "ort.c"
#include "mdx.c"
#include "cli.c"

int32
main(int32 argc, char **argv) {
    AudioBuffer input_audio;
    AudioBuffer output_audio;
    AudioIoFormat input_format;
    CliOptions options;
    StftPlan stft_plan;
    MdxConfig mdx_config;
    MdxModelInfo mdx_info;
    OrtContext ort_context;
    OrtModel ort_model;
    int32 parse_result;
    int32 result;

    program = argv[0];

    cli_options_init(&options);
    parse_result = cli_parse(&options, argc, argv);
    if (parse_result > 0) {
        return EXIT_SUCCESS;
    }
    if (parse_result < 0) {
        cli_print_usage(stderr);
    }

    result = EXIT_FAILURE;
    audio_buffer_init(&input_audio);
    audio_buffer_init(&output_audio);
    audio_io_format_init(&input_format);
    stft_plan_init_empty(&stft_plan);
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
    if (options.clip_mode == CLI_CLIP_MODE_NONE) {
        mdx_config.clip_mode = MDX_CLIP_MODE_NONE;
    }

    if (!audio_check_ffmpeg(options.ffmpeg_path)) {
        error2("could not run ffmpeg: %s\n", options.ffmpeg_path);
        error2("set --ffmpeg to a valid FFmpeg executable\n");
        goto cleanup;
    }

    if (!audio_can_decode_file(options.input_path, options.ffmpeg_path)) {
        error2("could not decode input with ffmpeg: %s\n",
                options.input_path);
        goto cleanup;
    }

    if (!util_file_exists(options.model_path)) {
        error2("could not read ONNX model: %s\n", options.model_path);
        goto cleanup;
    }

    if (!stft_plan_init(&stft_plan, mdx_config.n_fft, mdx_config.hop)) {
        error2(
            "could not initialize STFT plan for n_fft=%d hop=%d\n",
            mdx_config.n_fft,
            mdx_config.hop);
        goto cleanup;
    }

    if (!ort_context_init(&ort_context)) {
        error2("could not initialize ONNX Runtime\n");
        goto cleanup;
    }

    if (!ort_model_load(&ort_context, &ort_model, options.model_path)) {
        error2("could not load ONNX model: %s\n",
                options.model_path);
        goto cleanup;
    }

    if (!mdx_model_inspect(&mdx_info, &mdx_config, &ort_model)) {
        error2("ONNX model is not a supported MDX-Net model: %s\n",
                options.model_path);
        goto cleanup;
    }

    if (!mdx_config_prepare(&mdx_config)) {
        error2("could not prepare MDX configuration\n");
        goto cleanup;
    }

    input_format.sample_rate = mdx_config.sample_rate;
    input_format.channel_count = mdx_config.channel_count;
    if (!audio_read_file_format(&input_audio,
                                options.input_path,
                                &input_format,
                                options.ffmpeg_path)) {
        error2("could not decode input audio: %s\n",
                options.input_path);
        goto cleanup;
    }

    cli_print_options(&options);
    error2(
        "MDX model: input=%s output=%s shape=[%d, %d, %d, %d]\n",
        mdx_info.input_name,
        mdx_info.output_name,
        mdx_info.batch_size,
        mdx_info.channel_count,
        mdx_info.dim_f,
        mdx_info.dim_t);
    error2(
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
    error2(
        "decoded audio: sample_rate=%d channels=%d frames=%lld\n",
        input_audio.sample_rate,
        input_audio.channel_count,
        input_audio.frame_count);

    if (!mdx_process_song(&mdx_config,
                          &stft_plan,
                          &ort_context,
                          &ort_model,
                          &input_audio,
                          &output_audio)) {
        error2("could not process audio through MDX model\n");
        goto cleanup;
    }

    if (!audio_write_file(&output_audio,
                          options.output_path,
                          options.format,
                          options.ffmpeg_path)) {
        error2("could not write output audio: %s\n",
                options.output_path);
        goto cleanup;
    }

    error2("wrote extracted vocals: %s\n", options.output_path);
    result = EXIT_SUCCESS;

cleanup:
    audio_buffer_destroy(&output_audio);
    audio_buffer_destroy(&input_audio);
    ort_model_destroy(&ort_context, &ort_model);
    ort_context_destroy(&ort_context);
    stft_plan_destroy(&stft_plan);

    return result;
}
