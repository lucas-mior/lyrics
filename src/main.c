#if !defined(TESTING_app)
#define TESTING_app 0
#endif

#define CBASE_API_DECL static
#define CBASE_API_DEF static
#define CBASE_IMPLEMENT
#include "cbase.h"

#define LRC_PIPELINE_ENABLE_GENERATE 1
#include "lyricsync.h"
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

typedef struct MainOptions {
    LrcPipelineConfig config;

    char output_lrc_path[PATH_MAX];
    char lyrics_text_path[PATH_MAX];

    bool output_lrc_defaulted;
    bool onnx_provider_set;
    bool onnx_device_set;
    bool print_usage_on_error;
} MainOptions;

enum MainValueOptionKind {
    MAIN_VALUE_OPTION_INPUT_SONG,
    MAIN_VALUE_OPTION_INPUT_VOCALS,
    MAIN_VALUE_OPTION_OUTPUT_VOCALS,
    MAIN_VALUE_OPTION_INPUT_LYRICS,
    MAIN_VALUE_OPTION_OUTPUT_LRC,
    MAIN_VALUE_OPTION_MODEL_VOCAL,
    MAIN_VALUE_OPTION_MODEL_CTC,
    MAIN_VALUE_OPTION_TOKENIZER,
    MAIN_VALUE_OPTION_ONNX_PROVIDER,
    MAIN_VALUE_OPTION_ONNX_DEVICE,
    MAIN_VALUE_OPTION_FFMPEG,
    MAIN_VALUE_OPTION_TEMP_DIR,
    MAIN_VALUE_OPTION_VOCALS_FORMAT,
    MAIN_VALUE_OPTION_CHUNK_SECONDS,
    MAIN_VALUE_OPTION_MARGIN_SECONDS,
    MAIN_VALUE_OPTION_COMPENSATE,
    MAIN_VALUE_OPTION_N_FFT,
    MAIN_VALUE_OPTION_HOP,
    MAIN_VALUE_OPTION_DIM_F,
    MAIN_VALUE_OPTION_DIM_T,
    MAIN_VALUE_OPTION_MODEL_OUTPUT,
    MAIN_VALUE_OPTION_CLIP_MODE,
    MAIN_VALUE_OPTION_CTC_DEBUG_DUMP,
    MAIN_VALUE_OPTION_SPLIT_SIZE,
    MAIN_VALUE_OPTION_STAR_FREQUENCY,
    MAIN_VALUE_OPTION_ROMANIZATION,
    MAIN_VALUE_OPTION_LANGUAGE,
    MAIN_VALUE_OPTION_EMISSIONS,
};

typedef struct MainValueOption {
    char *name;
    enum MainValueOptionKind kind;
} MainValueOption;

enum MainFlagOptionKind {
    MAIN_FLAG_OPTION_HELP,
    MAIN_FLAG_OPTION_KEEP_TEMP_FILES,
    MAIN_FLAG_OPTION_ROMANIZE,
    MAIN_FLAG_OPTION_DENOISE,
};

typedef struct MainFlagOption {
    char *name;
    enum MainFlagOptionKind kind;
} MainFlagOption;

static void __attribute((noreturn))
main_print_usage(FILE *stream) {
    error2(
        "usage: %s (--input-song SONG | --input-vocals VOCALS) [options]\n"
        "\n"
        "task selection:\n"
        "    --input-song PATH          original song to process\n"
        "    --input-vocals PATH        already extracted vocals to use\n"
        "    --output-vocals PATH       save extracted vocals at PATH\n"
        "    --input-lyrics PATH        plain-text lyrics to align\n"
        "                                 [derived from input prefix]\n"
        "    --output-lrc PATH          synced lyrics output path\n"
        "\n"
        "model options:\n"
        "    --model-vocal PATH         MDX-Net ONNX model\n"
        "                                 ["
        LRC_DEFAULT_VOCALS_MODEL_PATH "]\n"
        "    --model-ctc PATH           CTC ONNX model\n"
        "                                 ["
        LRC_DEFAULT_CTC_MODEL_PATH "]\n"
        "    --tokenizer PATH           CTC tokenizer tokens file\n"
        "                                 ["
        LRC_DEFAULT_CTC_TOKENIZER_PATH "]\n"
        "    --onnx-provider KIND      auto|cpu|cuda [auto]\n"
        "    --onnx-device N           CUDA device id [0]\n"
        "\n"
        "audio options:\n"
        "    --ffmpeg PATH              ffmpeg executable [ffmpeg]\n"
        "    --temp-dir PATH            temporary directory [/tmp]\n"
        "    --vocals-format KIND       wav|flac|mp3|opus [inferred]\n"
        "    --chunk-seconds N          MDX chunk size in seconds [30]\n"
        "    --margin-seconds N         MDX chunk margin in seconds [3]\n"
        "    --denoise                  run denoising inference mode\n"
        "    --compensate X             output gain [1.035]\n"
        "    --n-fft N                  STFT size [6144]\n"
        "    --hop N                    STFT hop [1024]\n"
        "    --dim-f N                  override model frequency bins\n"
        "    --dim-t N                  override model time frames\n"
        "    --model-output vocals|instrumental [vocals]\n"
        "    --clip-mode clamp|none     final clipping policy [clamp]\n"
        "\n"
        "lyrics options:\n"
        "    --keep-temp-files          keep generated temporary files\n"
        "    --ctc-debug-dump PATH      write CTC parity debug dump\n"
        "    --split-size KIND          current|word|char [word]\n"
        "    --star-frequency KIND      none|edges|segment [edges]\n"
        "    --romanize                 select ICU romanization\n"
        "    --romanization KIND        off|icu [icu]\n"
        "    --language CODE            3-letter language code [eng]\n"
        "    --emissions KIND           logits|probabilities|"
        "log-probabilities\n",
        program
    );
    if (stream == stdout) {
        exit(EXIT_SUCCESS);
    }

    exit(EXIT_FAILURE);
}

static bool
main_long_option_value(char *arg, char *name, char **value) {
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
main_parse_int32(char *value, char *name, int32 *out) {
    char *end;
    int64 result;

    errno = 0;
    end = NULL;
    result = strtoll(value, &end, 10);
    if ((errno != 0) || (end == value) || (*end != '\0')) {
        error2("%s must be an integer: %s\n", name, value);
        return false;
    }
    if ((result < 0) || (result > INT32_MAX)) {
        error2("%s is outside the supported range: %s\n", name, value);
        return false;
    }

    *out = (int32)result;

    return true;
}

static bool
main_parse_positive_int32(char *value, char *name, int32 *out) {
    if (!main_parse_int32(value, name, out)) {
        return false;
    }
    if (*out <= 0) {
        error2("%s must be greater than zero: %s\n", name, value);
        return false;
    }

    return true;
}

static bool
main_parse_float(char *value, char *name, float *out) {
    char *end;
    float result;

    errno = 0;
    end = NULL;
    result = strtof(value, &end);
    if ((errno != 0) || (end == value) || (*end != '\0')) {
        error2("%s must be a number: %s\n", name, value);
        return false;
    }
    if (result < 0.0f) {
        error2("%s must not be negative: %s\n", name, value);
        return false;
    }

    *out = result;

    return true;
}

static bool
main_parse_emissions(LrcPipelineConfig *config, char *value) {
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
main_parse_onnx_provider(LrcPipelineConfig *config, char *value) {
    enum OrtExecutionProvider provider;

    if (ort_execution_provider_parse(value, &provider)) {
        config->ort_session_config.execution_provider = provider;
        return true;
    }

    error2("--onnx-provider must be auto, cpu, or cuda\n");

    return false;
}

static bool
main_parse_vocals_format(LrcPipelineConfig *config, char *value) {
    if (strequal(value, "wav")
        || strequal(value, "flac")
        || strequal(value, "mp3")
        || strequal(value, "opus")) {
        config->vocals_container_format = value;
        return true;
    }

    error2("--vocals-format must be wav, flac, mp3, or opus\n");

    return false;
}

static void
main_infer_vocals_format(LrcPipelineConfig *config, char *path) {
    char *extension;
    char *last_slash;
    int32 path_len;

    if (path_missing(path)) {
        return;
    }

    path_len = strlen32(path);
    extension = NULL;
    last_slash = NULL;
    for (int32 i = 0; i < path_len; i += 1) {
        if (path[i] == '/') {
            last_slash = path + i;
            extension = NULL;
            continue;
        }
        if (path[i] == '.') {
            extension = path + i + 1;
        }
    }

    if ((extension == NULL) || (extension[0] == '\0')) {
        return;
    }
    if (last_slash && (extension <= last_slash + 1)) {
        return;
    }
    if (strequal(extension, "wav")
        || strequal(extension, "flac")
        || strequal(extension, "mp3")
        || strequal(extension, "opus")) {
        config->vocals_container_format = extension;
    }

    return;
}

static bool
main_parse_model_output(LrcPipelineConfig *config, char *value) {
    if (strequal(value, "vocals")) {
        config->mdx_config.model_output = MDX_MODEL_OUTPUT_VOCALS;
        return true;
    }
    if (strequal(value, "instrumental")) {
        config->mdx_config.model_output = MDX_MODEL_OUTPUT_INSTRUMENTAL;
        return true;
    }

    error2("--model-output must be vocals or instrumental\n");

    return false;
}

static bool
main_parse_clip_mode(LrcPipelineConfig *config, char *value) {
    if (strequal(value, "clamp")) {
        config->mdx_config.clip_mode = MDX_CLIP_MODE_CLAMP;
        return true;
    }
    if (strequal(value, "none")) {
        config->mdx_config.clip_mode = MDX_CLIP_MODE_NONE;
        return true;
    }

    error2("--clip-mode must be clamp or none\n");

    return false;
}

static MainValueOption main_value_options[] = {
    {"--input-song", MAIN_VALUE_OPTION_INPUT_SONG},
    {"--input-vocals", MAIN_VALUE_OPTION_INPUT_VOCALS},
    {"--output-vocals", MAIN_VALUE_OPTION_OUTPUT_VOCALS},
    {"--vocals-output", MAIN_VALUE_OPTION_OUTPUT_VOCALS},
    {"--input-lyrics", MAIN_VALUE_OPTION_INPUT_LYRICS},
    {"--lyrics", MAIN_VALUE_OPTION_INPUT_LYRICS},
    {"-l", MAIN_VALUE_OPTION_INPUT_LYRICS},
    {"--output-lrc", MAIN_VALUE_OPTION_OUTPUT_LRC},
    {"--output", MAIN_VALUE_OPTION_OUTPUT_LRC},
    {"-o", MAIN_VALUE_OPTION_OUTPUT_LRC},
    {"--model-vocal", MAIN_VALUE_OPTION_MODEL_VOCAL},
    {"--model-ctc", MAIN_VALUE_OPTION_MODEL_CTC},
    {"--tokenizer", MAIN_VALUE_OPTION_TOKENIZER},
    {"--onnx-provider", MAIN_VALUE_OPTION_ONNX_PROVIDER},
    {"--onnx-device", MAIN_VALUE_OPTION_ONNX_DEVICE},
    {"--ffmpeg", MAIN_VALUE_OPTION_FFMPEG},
    {"--temp-dir", MAIN_VALUE_OPTION_TEMP_DIR},
    {"--vocals-format", MAIN_VALUE_OPTION_VOCALS_FORMAT},
    {"--format", MAIN_VALUE_OPTION_VOCALS_FORMAT},
    {"--chunk-seconds", MAIN_VALUE_OPTION_CHUNK_SECONDS},
    {"--margin-seconds", MAIN_VALUE_OPTION_MARGIN_SECONDS},
    {"--compensate", MAIN_VALUE_OPTION_COMPENSATE},
    {"--n-fft", MAIN_VALUE_OPTION_N_FFT},
    {"--hop", MAIN_VALUE_OPTION_HOP},
    {"--dim-f", MAIN_VALUE_OPTION_DIM_F},
    {"--dim-t", MAIN_VALUE_OPTION_DIM_T},
    {"--model-output", MAIN_VALUE_OPTION_MODEL_OUTPUT},
    {"--clip-mode", MAIN_VALUE_OPTION_CLIP_MODE},
    {"--ctc-debug-dump", MAIN_VALUE_OPTION_CTC_DEBUG_DUMP},
    {"--split-size", MAIN_VALUE_OPTION_SPLIT_SIZE},
    {"--star-frequency", MAIN_VALUE_OPTION_STAR_FREQUENCY},
    {"--romanization", MAIN_VALUE_OPTION_ROMANIZATION},
    {"--language", MAIN_VALUE_OPTION_LANGUAGE},
    {"--emissions", MAIN_VALUE_OPTION_EMISSIONS},
};

static MainFlagOption main_flag_options[] = {
    {"-h", MAIN_FLAG_OPTION_HELP},
    {"--help", MAIN_FLAG_OPTION_HELP},
    {"--keep-temp-files", MAIN_FLAG_OPTION_KEEP_TEMP_FILES},
    {"--romanize", MAIN_FLAG_OPTION_ROMANIZE},
    {"--denoise", MAIN_FLAG_OPTION_DENOISE},
};

static MainValueOption *
main_find_value_option(char *option) {
    for (int32 i = 0; i < LENGTH(main_value_options); i += 1) {
        if (strequal(option, main_value_options[i].name)) {
            return main_value_options + i;
        }
    }

    return NULL;
}

static MainFlagOption *
main_find_flag_option(char *option) {
    for (int32 i = 0; i < LENGTH(main_flag_options); i += 1) {
        if (strequal(option, main_flag_options[i].name)) {
            return main_flag_options + i;
        }
    }

    return NULL;
}

static bool
main_apply_value_option(
    MainOptions *options,
    enum MainValueOptionKind kind,
    char *option,
    char *value
) {
    LrcPipelineConfig *config;

    config = &options->config;
    switch (kind) {
    case MAIN_VALUE_OPTION_INPUT_SONG:
        config->song_path = value;
        break;
    case MAIN_VALUE_OPTION_INPUT_VOCALS:
        config->existing_vocals_path = value;
        break;
    case MAIN_VALUE_OPTION_OUTPUT_VOCALS:
        config->vocals_path = value;
        main_infer_vocals_format(config, value);
        break;
    case MAIN_VALUE_OPTION_INPUT_LYRICS:
        config->lyrics_text_path = value;
        break;
    case MAIN_VALUE_OPTION_OUTPUT_LRC:
        config->output_lrc_path = value;
        options->output_lrc_defaulted = false;
        break;
    case MAIN_VALUE_OPTION_MODEL_VOCAL:
        config->vocals_model_path = value;
        break;
    case MAIN_VALUE_OPTION_MODEL_CTC:
        config->ctc_model_path = value;
        break;
    case MAIN_VALUE_OPTION_TOKENIZER:
        config->tokenizer_path = value;
        break;
    case MAIN_VALUE_OPTION_ONNX_PROVIDER:
        options->onnx_provider_set = true;
        if (!main_parse_onnx_provider(config, value)) {
            return false;
        }
        break;
    case MAIN_VALUE_OPTION_ONNX_DEVICE:
        options->onnx_device_set = true;
        if (!main_parse_int32(value,
                              option,
                              &config->ort_session_config.device_id)) {
            return false;
        }
        break;
    case MAIN_VALUE_OPTION_FFMPEG:
        config->ffmpeg_path = value;
        break;
    case MAIN_VALUE_OPTION_TEMP_DIR:
        config->temp_dir = value;
        break;
    case MAIN_VALUE_OPTION_VOCALS_FORMAT:
        if (!main_parse_vocals_format(config, value)) {
            return false;
        }
        break;
    case MAIN_VALUE_OPTION_CHUNK_SECONDS:
        if (!main_parse_positive_int32(value,
                                       option,
                                       &config->mdx_config.chunk_seconds)) {
            return false;
        }
        break;
    case MAIN_VALUE_OPTION_MARGIN_SECONDS:
        if (!main_parse_int32(value,
                              option,
                              &config->mdx_config.margin_seconds)) {
            return false;
        }
        break;
    case MAIN_VALUE_OPTION_COMPENSATE:
        if (!main_parse_float(value, option, &config->mdx_config.compensate)) {
            return false;
        }
        break;
    case MAIN_VALUE_OPTION_N_FFT:
        if (!main_parse_positive_int32(value,
                                       option,
                                       &config->mdx_config.n_fft)) {
            return false;
        }
        break;
    case MAIN_VALUE_OPTION_HOP:
        if (!main_parse_positive_int32(value,
                                       option,
                                       &config->mdx_config.hop)) {
            return false;
        }
        break;
    case MAIN_VALUE_OPTION_DIM_F:
        if (!main_parse_positive_int32(value,
                                       option,
                                       &config->mdx_config.dim_f)) {
            return false;
        }
        break;
    case MAIN_VALUE_OPTION_DIM_T:
        if (!main_parse_positive_int32(value,
                                       option,
                                       &config->mdx_config.dim_t)) {
            return false;
        }
        break;
    case MAIN_VALUE_OPTION_MODEL_OUTPUT:
        if (!main_parse_model_output(config, value)) {
            return false;
        }
        break;
    case MAIN_VALUE_OPTION_CLIP_MODE:
        if (!main_parse_clip_mode(config, value)) {
            return false;
        }
        break;
    case MAIN_VALUE_OPTION_CTC_DEBUG_DUMP:
        config->ctc_debug_dump_path = value;
        break;
    case MAIN_VALUE_OPTION_SPLIT_SIZE:
        if (!lrc_pipeline_parse_preprocess_split_size(config, value)) {
            return false;
        }
        break;
    case MAIN_VALUE_OPTION_STAR_FREQUENCY:
        if (!lrc_pipeline_parse_preprocess_star_frequency(config, value)) {
            return false;
        }
        break;
    case MAIN_VALUE_OPTION_ROMANIZATION:
        if (!lrc_pipeline_parse_preprocess_romanization(config, value)) {
            return false;
        }
        break;
    case MAIN_VALUE_OPTION_LANGUAGE:
        if (!lrc_pipeline_parse_preprocess_language(config, value)) {
            return false;
        }
        break;
    case MAIN_VALUE_OPTION_EMISSIONS:
        if (!main_parse_emissions(config, value)) {
            return false;
        }
        break;
    default:
        error2("internal error: unhandled value option: %s\n", option);
        return false;
    }

    return true;
}

static bool
main_parse_value_option(MainOptions *options, char *option, char *value) {
    MainValueOption *value_option;

    if ((value_option = main_find_value_option(option)) == NULL) {
        error2("unknown option: %s\n", option);
        return false;
    }

    return main_apply_value_option(options,
                                   value_option->kind,
                                   option,
                                   value);
}

static bool
main_value_option_allows_equals(char *option) {
    if (option[0] != '-') {
        return false;
    }
    if (option[1] != '-') {
        return false;
    }

    return true;
}

static int32
main_parse_long_value(MainOptions *options, char *arg) {
    MainValueOption *value_option;
    char *value;

    for (int32 i = 0; i < LENGTH(main_value_options); i += 1) {
        value_option = main_value_options + i;
        if (!main_value_option_allows_equals(value_option->name)) {
            continue;
        }

        value = NULL;
        if (!main_long_option_value(arg, value_option->name, &value)) {
            continue;
        }
        if (!main_apply_value_option(options,
                                     value_option->kind,
                                     value_option->name,
                                     value)) {
            return -1;
        }

        return 1;
    }

    return 0;
}

static bool
main_option_needs_value(char *option) {
    if (main_find_value_option(option)) {
        return true;
    }

    return false;
}

static bool
main_apply_flag_option(
    MainOptions *options,
    enum MainFlagOptionKind kind
) {
    switch (kind) {
    case MAIN_FLAG_OPTION_HELP:
        main_print_usage(stdout);
    case MAIN_FLAG_OPTION_KEEP_TEMP_FILES:
        options->config.keep_temp_files = true;
        break;
    case MAIN_FLAG_OPTION_ROMANIZE:
        lrc_pipeline_enable_preprocess_romanization(&options->config);
        break;
    case MAIN_FLAG_OPTION_DENOISE:
        options->config.mdx_config.denoise = true;
        break;
    default:
        error2("internal error: unhandled flag option\n");
        return false;
    }

    return true;
}

static int32
main_parse_flag_option(MainOptions *options, char *option) {
    MainFlagOption *flag_option;

    if ((flag_option = main_find_flag_option(option)) == NULL) {
        return 0;
    }
    if (!main_apply_flag_option(options, flag_option->kind)) {
        return -1;
    }

    return 1;
}

static void
main_apply_onnx_provider_env(LrcPipelineConfig *config) {
    enum OrtExecutionProvider provider;
    char *env;

    env = getenv("LRC_ONNX_PROVIDER");
    if ((env == NULL) || (env[0] == '\0')) {
        return;
    }
    if (!ort_execution_provider_parse(env, &provider)) {
        error2("warning: LRC_ONNX_PROVIDER must be auto, cpu, or cuda\n");
        return;
    }
    if (config->ort_session_config.execution_provider
        == ORT_EXECUTION_PROVIDER_AUTO) {
        config->ort_session_config.execution_provider = provider;
    }

    return;
}

static void
main_apply_onnx_device_env(LrcPipelineConfig *config) {
    char *env;
    int32 device_id;

    env = getenv("LRC_ONNX_DEVICE");
    if ((env == NULL) || (env[0] == '\0')) {
        return;
    }
    if (!main_parse_int32(env, "LRC_ONNX_DEVICE", &device_id)) {
        error2("warning: ignoring invalid LRC_ONNX_DEVICE\n");
        return;
    }
    if (config->ort_session_config.device_id == 0) {
        config->ort_session_config.device_id = device_id;
    }

    return;
}

static void
main_apply_model_defaults(LrcPipelineConfig *config) {
    char *env;

    env = getenv("LRC_VOCALS_MODEL");
    if ((config->vocals_model_path == NULL)
        && env
        && (env[0] != '\0')) {
        config->vocals_model_path = env;
    }
    if (config->vocals_model_path == NULL) {
        config->vocals_model_path = LRC_DEFAULT_VOCALS_MODEL_PATH;
    }

    env = getenv("LRC_CTC_MODEL");
    if ((config->ctc_model_path == NULL)
        && env
        && (env[0] != '\0')) {
        config->ctc_model_path = env;
    }
    if (config->ctc_model_path == NULL) {
        config->ctc_model_path = LRC_DEFAULT_CTC_MODEL_PATH;
    }

    env = getenv("LRC_CTC_TOKENIZER");
    if ((config->tokenizer_path == NULL)
        && env
        && (env[0] != '\0')) {
        config->tokenizer_path = env;
    }
    if (config->tokenizer_path == NULL) {
        config->tokenizer_path = LRC_DEFAULT_CTC_TOKENIZER_PATH;
    }

    main_apply_onnx_provider_env(config);
    main_apply_onnx_device_env(config);

    return;
}

static bool
main_input_prefix_path(
    MainOptions *options,
    char *output_path,
    int64 output_size,
    char *extension,
    char *description
) {
    LrcPipelineConfig *config;
    char *input_path;
    int32 input_len;
    int32 slash_index;
    int32 dot_index;
    int32 prefix_len;
    int32 len;

    config = &options->config;
    input_path = config->song_path;
    if (path_missing(input_path)) {
        input_path = config->existing_vocals_path;
    }
    if (path_missing(input_path)) {
        error2("could not derive %s path without an input path\n",
               description);
        return false;
    }

    input_len = strlen32(input_path);
    slash_index = -1;
    dot_index = -1;
    for (int32 i = 0; i < input_len; i += 1) {
        if (input_path[i] == '/') {
            slash_index = i;
            dot_index = -1;
            continue;
        }
        if (input_path[i] == '.') {
            dot_index = i;
        }
    }

    prefix_len = input_len;
    if (dot_index > slash_index + 1) {
        prefix_len = dot_index;
    }
    len = snprintf2(output_path,
                    output_size,
                    "%.*s.%s",
                    prefix_len,
                    input_path,
                    extension);
    if ((len <= 0) || (len >= output_size)) {
        error2("default %s path is too long: %s\n",
               description,
               input_path);
        return false;
    }

    return true;
}

static bool
main_default_lyrics_text_path(MainOptions *options) {
    LrcPipelineConfig *config;

    config = &options->config;
    if (!path_missing(config->lyrics_text_path)) {
        return true;
    }
    if (!main_input_prefix_path(options,
                                options->lyrics_text_path,
                                SIZEOF(options->lyrics_text_path),
                                "txt",
                                "lyrics text")) {
        return false;
    }
    if (!util_file_exists(options->lyrics_text_path)) {
        error2("missing --input-lyrics and default lyrics file does not "
               "exist: %s\n",
               options->lyrics_text_path);
        return false;
    }

    config->lyrics_text_path = options->lyrics_text_path;

    return true;
}

static bool
main_default_lrc_path(MainOptions *options) {
    LrcPipelineConfig *config;

    config = &options->config;
    if (!main_input_prefix_path(options,
                                options->output_lrc_path,
                                SIZEOF(options->output_lrc_path),
                                "lrc",
                                "LRC output")) {
        return false;
    }

    config->output_lrc_path = options->output_lrc_path;
    options->output_lrc_defaulted = true;

    return true;
}

static void
main_mark_usage_error(MainOptions *options) {
    options->print_usage_on_error = true;

    return;
}

static bool
main_validate_options(MainOptions *options) {
    LrcPipelineConfig *config;
    bool has_song;
    bool has_vocals;
    bool has_lyrics;

    config = &options->config;
    has_song = !path_missing(config->song_path);
    has_vocals = !path_missing(config->existing_vocals_path);
    if (has_song && has_vocals) {
        error2("--input-song and --input-vocals cannot both be passed\n");
        main_mark_usage_error(options);
        return false;
    }
    if (!has_song && !has_vocals) {
        error2("missing required option: --input-song or --input-vocals\n");
        main_mark_usage_error(options);
        return false;
    }
    if (!main_default_lyrics_text_path(options)) {
        return false;
    }

    has_lyrics = !path_missing(config->lyrics_text_path);
    if (has_lyrics && path_missing(config->output_lrc_path)) {
        if (!main_default_lrc_path(options)) {
            return false;
        }
    }
    if (options->output_lrc_defaulted
        && util_file_exists(config->output_lrc_path)) {
        error2("default LRC output already exists: %s\n",
               config->output_lrc_path);
        return false;
    }
    if (has_lyrics && path_missing(config->output_lrc_path)) {
        error2("missing LRC output path\n");
        main_mark_usage_error(options);
        return false;
    }
    if (has_song && path_missing(config->vocals_model_path)) {
        error2("missing required option: --model-vocal\n");
        main_mark_usage_error(options);
        return false;
    }
    if (has_lyrics && path_missing(config->ctc_model_path)) {
        error2("missing required option: --model-ctc\n");
        main_mark_usage_error(options);
        return false;
    }
    if (has_lyrics && path_missing(config->tokenizer_path)) {
        error2("missing required option: --tokenizer\n");
        main_mark_usage_error(options);
        return false;
    }
    if (config->mdx_config.margin_seconds < 0) {
        error2("--margin-seconds must not be negative\n");
        main_mark_usage_error(options);
        return false;
    }
    if (config->ort_session_config.device_id < 0) {
        error2("--onnx-device must not be negative\n");
        main_mark_usage_error(options);
        return false;
    }

    return true;
}

static bool
main_parse_args(MainOptions *options, int32 argc, char **argv) {
    enum OrtExecutionProvider provider;
    int32 device_id;
    int32 parsed;

    if ((options == NULL) || (argc < 0)) {
        return false;
    }
    if ((argc > 0) && (argv == NULL)) {
        error2("missing command line argument vector\n");
        main_mark_usage_error(options);
        return false;
    }

    for (int32 i = 1; i < argc; i += 1) {
        if (argv[i] == NULL) {
            error2("missing command line argument %d\n", i);
            main_mark_usage_error(options);
            return false;
        }
        parsed = main_parse_flag_option(options, argv[i]);
        if (parsed > 0) {
            continue;
        }
        if (parsed < 0) {
            main_mark_usage_error(options);
            return false;
        }

        parsed = main_parse_long_value(options, argv[i]);
        if (parsed > 0) {
            continue;
        }
        if (parsed < 0) {
            main_mark_usage_error(options);
            return false;
        }
        if (!main_option_needs_value(argv[i])) {
            error2("unknown option: %s\n", argv[i]);
            main_mark_usage_error(options);
            return false;
        }
        if (i + 1 >= argc) {
            error2("%s requires a value\n", argv[i]);
            main_mark_usage_error(options);
            return false;
        }
        if (!main_parse_value_option(options, argv[i], argv[i + 1])) {
            main_mark_usage_error(options);
            return false;
        }
        i += 1;
    }

    provider = options->config.ort_session_config.execution_provider;
    device_id = options->config.ort_session_config.device_id;
    main_apply_model_defaults(&options->config);
    if (options->onnx_provider_set) {
        options->config.ort_session_config.execution_provider = provider;
    }
    if (options->onnx_device_set) {
        options->config.ort_session_config.device_id = device_id;
    }

    return main_validate_options(options);
}

static int32
main_generate_lrc(LrcPipelineConfig *config) {
    LrcPipeline pipeline;
    LrcPipelineGenerateResult result;
    int32 exit_status;

    lrc_pipeline_init(&pipeline, config);
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

    return exit_status;
}

LYRICS_API void
lyrics_config_init(LrcPipelineConfig *config) {
    if (config == NULL) {
        return;
    }

    lrc_pipeline_config_init(config);
    main_apply_model_defaults(config);

    return;
}

LYRICS_API bool
lyrics_extract_vocals(
    LrcPipelineConfig *config,
    LrcVocalsExtractResult *result
) {
    LrcPipeline pipeline;
    bool ok;

    if (config == NULL) {
        lrc_vocals_extract_result_init(result);
        if (result) {
            result->error = LRC_VOCALS_EXTRACT_ERROR_INVALID_ARGUMENT;
            result->message = "missing pipeline config";
        }
        return false;
    }

    main_apply_model_defaults(config);
    lrc_pipeline_init(&pipeline, config);
    ok = lrc_pipeline_extract_vocals(&pipeline, result);
    lrc_pipeline_cleanup(&pipeline);

    return ok;
}

LYRICS_API bool
lyrics_generate_lrc(
    LrcPipelineConfig *config,
    LrcPipelineGenerateResult *result
) {
    LrcPipeline pipeline;
    bool ok;

    if (config == NULL) {
        lrc_pipeline_generate_result_init(result);
        if (result) {
            result->error = LRC_PIPELINE_GENERATE_ERROR_INVALID_ARGUMENT;
            result->message = "missing pipeline config";
        }
        return false;
    }

    main_apply_model_defaults(config);
    lrc_pipeline_init(&pipeline, config);
    ok = lrc_pipeline_generate_lrc(&pipeline, result);
    lrc_pipeline_cleanup(&pipeline);

    return ok;
}

LYRICS_API int32
lyrics_main(int32 argc, char **argv) {
    MainOptions options;
    bool has_lyrics;

    if ((argc > 0) && argv) {
        program = argv[0];
    }

    memset64(&options, 0, SIZEOF(options));
    lrc_pipeline_config_init(&options.config);
    if (!main_parse_args(&options, argc, argv)) {
        if (options.print_usage_on_error) {
            main_print_usage(stderr);
        }
        return EXIT_FAILURE;
    }

    has_lyrics = !path_missing(options.config.lyrics_text_path);
    if (!has_lyrics) {
        error2("internal error: lyrics path missing after validation\n");
        return EXIT_FAILURE;
    }

    if (!path_missing(options.config.existing_vocals_path)
        && !path_missing(options.config.vocals_path)) {
        error2("warning: --output-vocals ignored when --input-vocals is "
               "used\n");
    }

    return main_generate_lrc(&options.config);
}

#if !defined(LYRICS_BUILD_SHARED) || !LYRICS_BUILD_SHARED
int32
main(int32 argc, char **argv) {
    exit(lyrics_main(argc, argv));
}
#endif
