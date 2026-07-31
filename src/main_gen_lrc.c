#if !defined(TESTING_app)
#define TESTING_app 0
#endif

#define CBASE_API_DECL static
#define CBASE_API_DEF static
#define CBASE_IMPLEMENT
#include "cbase.h"

#define LRC_PIPELINE_ENABLE_GENERATE 1
#include "pipeline.h"
#include "default_models.h"

#include "fftw.c"
#include "stft.c"
#include "audio.c"
#include "ort.c"
#include "mdx.c"
#include "vocals.c"
#include "lyrics.c"
#include "unicode_norm.c"
#include "ctc_text.c"
#include "ctc_assets.c"
#include "ctc_tokenizer.c"
#include "ctc_audio.c"
#include "ctc_model.c"
#include "ctc_inference.c"
#include "ctc_align.c"
#include "lrc.c"
#include "pipeline.c"

static void __attribute((noreturn))
full_print_usage(FILE *stream) {
    error2(
        "usage: %s -i SONG -l LYRICS.txt -o OUTPUT.lrc [options]\n"
        "\n"
        "options:\n"
        "    --model-vocal PATH         MDX-Net ONNX model\n"
        "                                 ["
        LRC_DEFAULT_VOCALS_MODEL_PATH "]\n"
        "    --model-ctc PATH            CTC ONNX model\n"
        "                                 ["
        LRC_DEFAULT_CTC_MODEL_PATH "]\n"
        "    --tokenizer PATH            CTC tokenizer tokens file\n"
        "                                 ["
        LRC_DEFAULT_CTC_TOKENIZER_PATH "]\n"
        "    --vocals-output PATH         keep extracted vocals at PATH\n"
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
full_path_missing(char *path) {
    if (path == NULL) {
        return true;
    }
    if (path[0] == '\0') {
        return true;
    }

    return false;
}

static bool
full_long_option_value(char *arg, char *name, char **value) {
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
full_parse_emissions(
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
full_parse_value_option(
    LrcPipelineConfig *config,
    char *option,
    char *value
) {
    if (strequal(option, "-i") || strequal(option, "--input")) {
        config->song_path = value;
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
    if (strequal(option, "--model-vocal")) {
        config->vocals_model_path = value;
        return true;
    }
    if (strequal(option, "--vocals-output")) {
        config->vocals_path = value;
        return true;
    }
    if (strequal(option, "--model-ctc")) {
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
        return full_parse_emissions(config, value);
    }

    error2("unknown option: %s\n", option);

    return false;
}

static bool
full_parse_long_value(
    LrcPipelineConfig *config,
    char *arg
) {
    char *value;

    value = NULL;
    if (full_long_option_value(arg, "--input", &value)) {
        config->song_path = value;
        return true;
    }
    if (full_long_option_value(arg, "--lyrics", &value)) {
        config->lyrics_text_path = value;
        return true;
    }
    if (full_long_option_value(arg, "--output", &value)) {
        config->output_lrc_path = value;
        return true;
    }
    if (full_long_option_value(arg, "--model-vocal", &value)) {
        config->vocals_model_path = value;
        return true;
    }
    if (full_long_option_value(arg, "--vocals-output", &value)) {
        config->vocals_path = value;
        return true;
    }
    if (full_long_option_value(arg, "--model-ctc", &value)) {
        config->ctc_model_path = value;
        return true;
    }
    if (full_long_option_value(arg, "--tokenizer", &value)) {
        config->tokenizer_path = value;
        return true;
    }
    if (full_long_option_value(arg, "--ffmpeg", &value)) {
        config->ffmpeg_path = value;
        return true;
    }
    if (full_long_option_value(arg, "--temp-dir", &value)) {
        config->temp_dir = value;
        return true;
    }
    if (full_long_option_value(arg, "--emissions", &value)) {
        return full_parse_emissions(config, value);
    }

    return false;
}

static bool
full_option_needs_value(char *option) {
    return strequal(option, "-i")
           || strequal(option, "--input")
           || strequal(option, "-l")
           || strequal(option, "--lyrics")
           || strequal(option, "-o")
           || strequal(option, "--output")
           || strequal(option, "--model-vocal")
           || strequal(option, "--vocals-output")
           || strequal(option, "--model-ctc")
           || strequal(option, "--tokenizer")
           || strequal(option, "--ffmpeg")
           || strequal(option, "--temp-dir")
           || strequal(option, "--emissions");
}

static bool
full_parse_args(
    LrcPipelineConfig *config,
    int32 argc,
    char **argv
) {
    char *env;

    for (int32 i = 1; i < argc; i += 1) {
        if (strequal(argv[i], "-h") || strequal(argv[i], "--help")) {
            full_print_usage(stdout);
        }
        if (strequal(argv[i], "--keep-temp-files")) {
            config->keep_temp_files = true;
            continue;
        }
        if (full_parse_long_value(config, argv[i])) {
            continue;
        }
        if (!full_option_needs_value(argv[i])) {
            error2("unknown option: %s\n", argv[i]);
            return false;
        }
        if (i + 1 >= argc) {
            error2("%s requires a value\n", argv[i]);
            return false;
        }
        if (!full_parse_value_option(config, argv[i], argv[i + 1])) {
            return false;
        }
        i += 1;
    }

    env = getenv("LRC_VOCALS_MODEL");
    if ((config->vocals_model_path == NULL)
        && (env != NULL)
        && (env[0] != '\0')) {
        config->vocals_model_path = env;
    }
    if (config->vocals_model_path == NULL) {
        config->vocals_model_path = LRC_DEFAULT_VOCALS_MODEL_PATH;
    }

    env = getenv("LRC_CTC_MODEL");
    if ((config->ctc_model_path == NULL)
        && (env != NULL)
        && (env[0] != '\0')) {
        config->ctc_model_path = env;
    }
    if (config->ctc_model_path == NULL) {
        config->ctc_model_path = LRC_DEFAULT_CTC_MODEL_PATH;
    }

    env = getenv("LRC_CTC_TOKENIZER");
    if ((config->tokenizer_path == NULL)
        && (env != NULL)
        && (env[0] != '\0')) {
        config->tokenizer_path = env;
    }
    if (config->tokenizer_path == NULL) {
        config->tokenizer_path = LRC_DEFAULT_CTC_TOKENIZER_PATH;
    }

    if (full_path_missing(config->song_path)) {
        error2("missing required option: -i/--input\n");
        return false;
    }
    if (full_path_missing(config->lyrics_text_path)) {
        error2("missing required option: -l/--lyrics\n");
        return false;
    }
    if (full_path_missing(config->output_lrc_path)) {
        error2("missing required option: -o/--output\n");
        return false;
    }
    if (full_path_missing(config->vocals_model_path)) {
        error2("missing required option: --model-vocal\n");
        return false;
    }
    if (full_path_missing(config->ctc_model_path)) {
        error2("missing required option: --model-ctc\n");
        return false;
    }
    if (full_path_missing(config->tokenizer_path)) {
        error2("missing required option: --tokenizer\n");
        return false;
    }

    return true;
}

int32
main(int32 argc, char **argv) {
    LrcPipelineConfig config;
    LrcPipelineGenerateResult result;
    int32 exit_status;

    program = argv[0];

    lrc_pipeline_config_init(&config);
    if (!full_parse_args(&config, argc, argv)) {
        full_print_usage(stderr);
    }

    exit_status = EXIT_FAILURE;
    if (lrc_generate_from_song(&config, &result)) {
        exit_status = EXIT_SUCCESS;
    } else {
        error2("LRC generation failed: %s", result.message);
        if (result.path) {
            error2(": %s", result.path);
        }
        error2("\n");
    }
    exit(exit_status);
}
