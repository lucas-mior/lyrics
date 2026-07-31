#if !defined(TESTING_app)
#define TESTING_app 0
#endif

#define CBASE_API_DECL static
#define CBASE_API_DEF static
#define CBASE_IMPLEMENT
#include "cbase.h"

#define LRC_PIPELINE_ENABLE_GENERATE 1
#include "pipeline.h"

#include "audio.c"
#if LRC_CTC_INFERENCE_ENABLE_ORT
#include "ort.c"
#endif
#include "lyrics.c"
#include "ctc_assets.c"
#include "ctc_tokenizer.c"
#include "ctc_audio.c"
#include "ctc_model.c"
#include "ctc_inference.c"
#include "ctc_align.c"
#include "lrc.c"

static void
mdx_config_init(MdxConfig *config) {
    memset64(config, 0, SIZEOF(*config));

    config->sample_rate = 44100;
    config->channel_count = 2;
    config->dim_c = 4;
    config->n_fft = 6144;
    config->hop = 1024;
    config->chunk_seconds = 30;
    config->margin_seconds = 3;
    config->compensate = 1.035f;
    config->model_output = MDX_MODEL_OUTPUT_VOCALS;
    config->clip_mode = MDX_CLIP_MODE_CLAMP;

    return;
}

static void
lrc_vocals_extract_request_init(LrcVocalsExtractRequest *request) {
    memset64(request, 0, SIZEOF(*request));

    request->temp_dir = "/tmp";
    request->ffmpeg_path = "ffmpeg";
    request->container_format = "wav";
    request->print_info = true;
    audio_io_format_init(&request->output_format);
    mdx_config_init(&request->mdx_config);

    return;
}

static void
lrc_vocals_extract_result_init(LrcVocalsExtractResult *result) {
    result->error = LRC_VOCALS_EXTRACT_ERROR_NONE;
    result->message = "ok";
    result->path = NULL;

    return;
}

static bool
lrc_extract_vocals(
    LrcVocalsExtractRequest *request,
    LrcVocalsExtractResult *result
) {
    (void)request;
    (void)result;

    return false;
}

#include "pipeline.c"

static void __attribute((noreturn))
raw_print_usage(FILE *stream) {
    error2(
        "usage: %s -i VOCALS -l LYRICS.txt -o OUTPUT.lrc "
        "--ctc-model MODEL.onnx --tokenizer TOKENS.txt [options]\n"
        "\n"
        "options:\n"
        "    --ffmpeg PATH                ffmpeg executable [ffmpeg]\n"
        "    --temp-dir PATH              temporary directory [/tmp]\n"
        "    --keep-temp-files            keep generated temporary files\n"
        "    --emissions KIND             logits|probabilities|"
        "log-probabilities\n",
        program
    );
    if (stream == stdout) {
        exit(EXIT_SUCCESS);
    }

    exit(EXIT_FAILURE);
}

static bool
raw_long_option_value(char *arg, char *name, char **value) {
    int32 i;

    for (i = 0; name[i] != '\0'; i += 1) {
        if (arg[i] != name[i]) {
            return false;
        }
    }
    if (arg[i] != '=') {
        return false;
    }

    *value = arg + i + 1;

    return true;
}

static bool
raw_parse_emissions(
    LrcPipelineConfig *config,
    char *value
) {
    if (strequal(value, "logits")) {
        config->ctc_emission_values_kind = LRC_CTC_EMISSION_VALUES_LOGITS;
        return true;
    }
    if (strequal(value, "probabilities")) {
        config->ctc_emission_values_kind =
            LRC_CTC_EMISSION_VALUES_PROBABILITIES;
        return true;
    }
    if (strequal(value, "log-probabilities")) {
        config->ctc_emission_values_kind =
            LRC_CTC_EMISSION_VALUES_LOG_PROBABILITIES;
        return true;
    }

    error2("--emissions must be logits, probabilities, or log-probabilities\n");

    return false;
}

static bool
raw_parse_value_option(
    LrcPipelineConfig *config,
    char *option,
    char *value
) {
    if (strequal(option, "-i") || strequal(option, "--input")) {
        config->existing_vocals_path = value;
        return true;
    }
    if (strequal(option, "-l") || strequal(option, "--lyrics")) {
        config->lyrics_text_path = value;
        return true;
    }
    if (strequal(option, "-o") || strequal(option, "--output")) {
        config->output_lrc_path = value;
        return true;
    }
    if (strequal(option, "--ctc-model")) {
        config->ctc_model_path = value;
        return true;
    }
    if (strequal(option, "--tokenizer")) {
        config->tokenizer_path = value;
        return true;
    }
    if (strequal(option, "--ffmpeg")) {
        config->ffmpeg_path = value;
        return true;
    }
    if (strequal(option, "--temp-dir")) {
        config->temp_dir = value;
        return true;
    }
    if (strequal(option, "--emissions")) {
        return raw_parse_emissions(config, value);
    }

    error2("unknown option: %s\n", option);

    return false;
}

static bool
raw_parse_long_value(
    LrcPipelineConfig *config,
    char *arg
) {
    char *value;

    value = NULL;
    if (raw_long_option_value(arg, "--input", &value)) {
        config->existing_vocals_path = value;
        return true;
    }
    if (raw_long_option_value(arg, "--lyrics", &value)) {
        config->lyrics_text_path = value;
        return true;
    }
    if (raw_long_option_value(arg, "--output", &value)) {
        config->output_lrc_path = value;
        return true;
    }
    if (raw_long_option_value(arg, "--ctc-model", &value)) {
        config->ctc_model_path = value;
        return true;
    }
    if (raw_long_option_value(arg, "--tokenizer", &value)) {
        config->tokenizer_path = value;
        return true;
    }
    if (raw_long_option_value(arg, "--ffmpeg", &value)) {
        config->ffmpeg_path = value;
        return true;
    }
    if (raw_long_option_value(arg, "--temp-dir", &value)) {
        config->temp_dir = value;
        return true;
    }
    if (raw_long_option_value(arg, "--emissions", &value)) {
        return raw_parse_emissions(config, value);
    }

    return false;
}

static bool
raw_option_needs_value(char *option) {
    return strequal(option, "-i")
           || strequal(option, "--input")
           || strequal(option, "-l")
           || strequal(option, "--lyrics")
           || strequal(option, "-o")
           || strequal(option, "--output")
           || strequal(option, "--ctc-model")
           || strequal(option, "--tokenizer")
           || strequal(option, "--ffmpeg")
           || strequal(option, "--temp-dir")
           || strequal(option, "--emissions");
}

static bool
raw_parse_args(
    LrcPipelineConfig *config,
    int32 argc,
    char **argv
) {
    char *env;

    for (int32 i = 1; i < argc; i += 1) {
        if (strequal(argv[i], "-h") || strequal(argv[i], "--help")) {
            raw_print_usage(stdout);
        }
        if (strequal(argv[i], "--keep-temp-files")) {
            config->keep_temp_files = true;
            continue;
        }
        if (raw_parse_long_value(config, argv[i])) {
            continue;
        }
        if (!raw_option_needs_value(argv[i])) {
            error2("unknown option: %s\n", argv[i]);
            return false;
        }
        if (i + 1 >= argc) {
            error2("%s requires a value\n", argv[i]);
            return false;
        }
        if (!raw_parse_value_option(config, argv[i], argv[i + 1])) {
            return false;
        }
        i += 1;
    }

    env = getenv("LRC_CTC_MODEL");
    if ((config->ctc_model_path == NULL) && env) {
        config->ctc_model_path = env;
    }
    env = getenv("LRC_CTC_TOKENIZER");
    if ((config->tokenizer_path == NULL) && env) {
        config->tokenizer_path = env;
    }

    if (config->existing_vocals_path == NULL) {
        error2("missing required option: -i/--input\n");
        return false;
    }
    if (config->lyrics_text_path == NULL) {
        error2("missing required option: -l/--lyrics\n");
        return false;
    }
    if (config->output_lrc_path == NULL) {
        error2("missing required option: -o/--output\n");
        return false;
    }
    if (config->ctc_model_path == NULL) {
        error2("missing required option: --ctc-model\n");
        return false;
    }
    if (config->tokenizer_path == NULL) {
        error2("missing required option: --tokenizer\n");
        return false;
    }

    return true;
}

int32
main(int32 argc, char **argv) {
    LrcPipelineConfig config;
    LrcPipeline pipeline;
    LrcPipelineGenerateResult result;
    int32 exit_status;

    program = argv[0];

    lrc_pipeline_config_init(&config);
    if (!raw_parse_args(&config, argc, argv)) {
        raw_print_usage(stderr);
    }

    lrc_pipeline_init(&pipeline, &config);
    exit_status = EXIT_FAILURE;
    if (lrc_pipeline_generate_lrc(&pipeline, &result)) {
        exit_status = EXIT_SUCCESS;
    } else {
        error2("LRC generation failed: %s", result.message);
        if (result.path) {
            error2(": %s", result.path);
        }
        error2("\n");
    }

    lrc_pipeline_cleanup(&pipeline);
    exit(exit_status);
}
