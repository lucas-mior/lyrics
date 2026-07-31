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
#if !defined(TESTING_vocals)
#define TESTING_vocals 0
#endif

#define CBASE_API_DECL static
#define CBASE_API_DEF static
#define CBASE_IMPLEMENT
#include "cbase.h"

#include "cli.h"
#include "vocals.h"
#include "pipeline.h"

#include "fftw.c"
#include "stft.c"
#include "audio.c"
#include "ort.c"
#include "mdx.c"
#include "vocals.c"
#include "pipeline.c"
#include "cli.c"

static void
get_voice_pipeline_config_from_options(
    LrcPipelineConfig *config,
    CliOptions *options
) {
    lrc_pipeline_config_init(config);

    config->song_path = options->input_path;
    config->vocals_path = options->output_path;
    config->vocals_model_path = options->model_path;
    config->temp_dir = options->temp_dir;
    config->ffmpeg_path = options->ffmpeg_path;
    config->vocals_container_format = options->format;
    config->mdx_config.n_fft = options->n_fft;
    config->mdx_config.hop = options->hop;
    config->mdx_config.dim_f = options->dim_f;
    config->mdx_config.dim_t = options->dim_t;
    config->mdx_config.chunk_seconds = options->chunk_seconds;
    config->mdx_config.margin_seconds = options->margin_seconds;
    config->mdx_config.compensate = options->compensate;
    config->mdx_config.denoise = options->denoise;

    if (options->model_output == CLI_MODEL_OUTPUT_INSTRUMENTAL) {
        config->mdx_config.model_output = MDX_MODEL_OUTPUT_INSTRUMENTAL;
    }
    if (options->clip_mode == CLI_CLIP_MODE_NONE) {
        config->mdx_config.clip_mode = MDX_CLIP_MODE_NONE;
    }

    return;
}

int32
main(int32 argc, char **argv) {
    CliOptions options;
    LrcPipelineConfig config;
    LrcPipeline pipeline;
    LrcVocalsExtractResult extraction_result;
    int32 parse_result;
    int32 result;

    program = argv[0];

    cli_options_init(&options);
    parse_result = cli_parse(&options, argc, argv);
    if (parse_result > 0) {
        exit(EXIT_SUCCESS);
    }
    if (parse_result < 0) {
        cli_print_usage(stderr);
    }

    get_voice_pipeline_config_from_options(&config, &options);
    lrc_pipeline_init(&pipeline, &config);
    cli_print_options(&options);

    result = EXIT_FAILURE;
    if (lrc_pipeline_extract_vocals(&pipeline, &extraction_result)) {
        result = EXIT_SUCCESS;
    } else {
        error2("vocals extraction failed: %s", extraction_result.message);
        if (extraction_result.path) {
            error2(": %s", extraction_result.path);
        }
        error2("\n");
    }

    lrc_pipeline_cleanup(&pipeline);
    exit(result);
}
