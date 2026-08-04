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

#define MAIN_TASK_VALUE_OPTIONS(X) \
    X(INPUT_SONG, "--input-song", "PATH", \
      "original song to process", NULL, main_apply_input_song) \
    X(INPUT_VOCALS, "--input-vocals", "PATH", \
      "already extracted vocals to use", NULL, main_apply_input_vocals) \
    X(OUTPUT_VOCALS, "--output-vocals", "PATH", \
      "save extracted vocals at PATH", NULL, main_apply_output_vocals) \
    X(INPUT_LYRICS, "--input-lyrics", "PATH", \
      "plain-text lyrics to align", "derived from input prefix", \
      main_apply_input_lyrics) \
    X(OUTPUT_LRC, "--output-lrc", "PATH", \
      "synced lyrics output path", NULL, main_apply_output_lrc)

#define MAIN_MODEL_VALUE_OPTIONS(X) \
    X(MODEL_VOCAL, "--model-vocal", "PATH", \
      "MDX-Net ONNX model", LRC_DEFAULT_VOCALS_MODEL_PATH, \
      main_apply_model_vocal) \
    X(MODEL_CTC, "--model-ctc", "PATH", \
      "CTC ONNX model", LRC_DEFAULT_CTC_MODEL_PATH, \
      main_apply_model_ctc) \
    X(TOKENIZER, "--tokenizer", "PATH", \
      "CTC tokenizer tokens file", LRC_DEFAULT_CTC_TOKENIZER_PATH, \
      main_apply_tokenizer) \
    X(ONNX_PROVIDER, "--onnx-provider", "KIND", \
      "ONNX provider (" ORT_EXECUTION_PROVIDER_NAMES ")", "auto", \
      main_apply_onnx_provider) \
    X(ONNX_DEVICE, "--onnx-device", "N", \
      "CUDA device id", "0", main_apply_onnx_device)

#define MAIN_AUDIO_VALUE_OPTIONS(X) \
    X(FFMPEG, "--ffmpeg", "PATH", \
      "ffmpeg executable", "ffmpeg", main_apply_ffmpeg) \
    X(TEMP_DIR, "--temp-dir", "PATH", \
      "temporary directory", "/tmp", main_apply_temp_dir) \
    X(VOCALS_FORMAT, "--vocals-format", "KIND", \
      "extracted vocals container (" LRC_AUDIO_FORMAT_NAMES ")", \
      "inferred", main_apply_vocals_format) \
    X(CHUNK_SECONDS, "--chunk-seconds", "N", \
      "MDX chunk size in seconds", "30", main_apply_chunk_seconds) \
    X(MARGIN_SECONDS, "--margin-seconds", "N", \
      "MDX chunk margin in seconds", "3", main_apply_margin_seconds) \
    X(COMPENSATE, "--compensate", "X", \
      "output gain", "1.035", main_apply_compensate) \
    X(N_FFT, "--n-fft", "N", \
      "STFT size", "6144", main_apply_n_fft) \
    X(HOP, "--hop", "N", \
      "STFT hop", "1024", main_apply_hop) \
    X(DIM_F, "--dim-f", "N", \
      "override model frequency bins", NULL, main_apply_dim_f) \
    X(DIM_T, "--dim-t", "N", \
      "override model time frames", NULL, main_apply_dim_t) \
    X(MODEL_OUTPUT, "--model-output", "KIND", \
      "model output stem (" MDX_MODEL_OUTPUT_NAMES ")", "vocals", \
      main_apply_model_output) \
    X(CLIP_MODE, "--clip-mode", "KIND", \
      "final clipping policy (" MDX_CLIP_MODE_NAMES ")", "clamp", \
      main_apply_clip_mode)

#define MAIN_LYRICS_VALUE_OPTIONS(X) \
    X(CTC_DEBUG_DUMP, "--ctc-debug-dump", "PATH", \
      "write CTC parity debug dump", NULL, main_apply_ctc_debug_dump) \
    X(SPLIT_SIZE, "--split-size", "KIND", \
      "lyrics split size (current|word|char)", "word", \
      main_apply_split_size) \
    X(STAR_FREQUENCY, "--star-frequency", "KIND", \
      "star-token placement (none|edges|segment)", "edges", \
      main_apply_star_frequency) \
    X(ROMANIZATION, "--romanization", "KIND", \
      "romanization backend (off|icu)", "icu", \
      main_apply_romanization) \
    X(LANGUAGE, "--language", "CODE", \
      "3-letter language code", "eng", main_apply_language) \
    X(EMISSIONS, "--emissions", "KIND", \
      "model emission values (" LRC_CTC_EMISSION_VALUES_KIND_NAMES ")", \
      "logits", \
      main_apply_emissions)

#define MAIN_VALUE_OPTIONS(X) \
    MAIN_TASK_VALUE_OPTIONS(X) \
    MAIN_MODEL_VALUE_OPTIONS(X) \
    MAIN_AUDIO_VALUE_OPTIONS(X) \
    MAIN_LYRICS_VALUE_OPTIONS(X)

#define MAIN_VALUE_OPTION_ALIASES(X) \
    X(OUTPUT_VOCALS, "--vocals-output", main_apply_output_vocals) \
    X(INPUT_LYRICS, "--lyrics", main_apply_input_lyrics) \
    X(INPUT_LYRICS, "-l", main_apply_input_lyrics) \
    X(OUTPUT_LRC, "--output", main_apply_output_lrc) \
    X(OUTPUT_LRC, "-o", main_apply_output_lrc) \
    X(VOCALS_FORMAT, "--format", main_apply_vocals_format)

#define MAIN_GENERAL_FLAG_OPTIONS(X) \
    X(HELP, "--help", "show this help", main_apply_help)

#define MAIN_AUDIO_FLAG_OPTIONS(X) \
    X(DENOISE, "--denoise", "run denoising inference mode", \
      main_apply_denoise)

#define MAIN_LYRICS_FLAG_OPTIONS(X) \
    X(KEEP_TEMP_FILES, "--keep-temp-files", \
      "keep generated temporary files", main_apply_keep_temp_files) \
    X(ROMANIZE, "--romanize", "select ICU romanization", \
      main_apply_romanize)

#define MAIN_FLAG_OPTIONS(X) \
    MAIN_GENERAL_FLAG_OPTIONS(X) \
    MAIN_AUDIO_FLAG_OPTIONS(X) \
    MAIN_LYRICS_FLAG_OPTIONS(X)

#define MAIN_FLAG_OPTION_ALIASES(X) \
    X(HELP, "-h", main_apply_help)

enum MainValueOptionKind {
#define MAIN_VALUE_OPTION_ENUM(id, name, metavar, description, default_text, \
                               apply_fn) \
    MAIN_VALUE_OPTION_##id,
    MAIN_VALUE_OPTIONS(MAIN_VALUE_OPTION_ENUM)
#undef MAIN_VALUE_OPTION_ENUM
    MAIN_VALUE_OPTION_LAST,
};

enum MainFlagOptionKind {
#define MAIN_FLAG_OPTION_ENUM(id, name, description, apply_fn) \
    MAIN_FLAG_OPTION_##id,
    MAIN_FLAG_OPTIONS(MAIN_FLAG_OPTION_ENUM)
#undef MAIN_FLAG_OPTION_ENUM
    MAIN_FLAG_OPTION_LAST,
};

typedef bool MainValueOptionApplyFunction(
    MainOptions *options,
    char *option,
    char *value
);

typedef bool MainFlagOptionApplyFunction(MainOptions *options);

typedef struct MainValueOption {
    char *name;
    enum MainValueOptionKind kind;
    MainValueOptionApplyFunction *apply;
} MainValueOption;

typedef struct MainFlagOption {
    char *name;
    enum MainFlagOptionKind kind;
    MainFlagOptionApplyFunction *apply;
} MainFlagOption;

typedef struct MainEnumValue {
    char *name;
    int32 value;
} MainEnumValue;

static void
main_print_value_option_usage(
    char *name,
    char *metavar,
    char *description,
    char *default_text
) {
    char option[64];
    int32 len;

    len = snprintf2(option, SIZEOF(option), "%s %s", name, metavar);
    if ((len <= 0) || (len >= SIZEOF(option))) {
        option[0] = '\0';
    }

    error2("    %-28s %s", option, description);
    if (default_text != NULL) {
        error2(" [%s]", default_text);
    }
    error2("\n");

    return;
}

static void
main_print_flag_option_usage(char *name, char *description) {
    error2("    %-28s %s\n", name, description);

    return;
}

static void __attribute((noreturn))
main_print_usage(FILE *stream) {
    error2(
        "usage: %s (--input-song SONG | --input-vocals VOCALS) [options]\n",
        program
    );
    error2("\n");
    error2("general options:\n");
#define MAIN_PRINT_FLAG_USAGE(id, name, description, apply_fn) \
    main_print_flag_option_usage(name, description);
    MAIN_GENERAL_FLAG_OPTIONS(MAIN_PRINT_FLAG_USAGE)
#undef MAIN_PRINT_FLAG_USAGE

    error2("\n");
    error2("task selection:\n");
#define MAIN_PRINT_VALUE_USAGE(id, name, metavar, description, default_text, \
                               apply_fn) \
    main_print_value_option_usage(name, metavar, description, default_text);
    MAIN_TASK_VALUE_OPTIONS(MAIN_PRINT_VALUE_USAGE)
#undef MAIN_PRINT_VALUE_USAGE

    error2("\n");
    error2("model options:\n");
#define MAIN_PRINT_VALUE_USAGE(id, name, metavar, description, default_text, \
                               apply_fn) \
    main_print_value_option_usage(name, metavar, description, default_text);
    MAIN_MODEL_VALUE_OPTIONS(MAIN_PRINT_VALUE_USAGE)
#undef MAIN_PRINT_VALUE_USAGE

    error2("\n");
    error2("audio options:\n");
#define MAIN_PRINT_VALUE_USAGE(id, name, metavar, description, default_text, \
                               apply_fn) \
    main_print_value_option_usage(name, metavar, description, default_text);
    MAIN_AUDIO_VALUE_OPTIONS(MAIN_PRINT_VALUE_USAGE)
#undef MAIN_PRINT_VALUE_USAGE
#define MAIN_PRINT_FLAG_USAGE(id, name, description, apply_fn) \
    main_print_flag_option_usage(name, description);
    MAIN_AUDIO_FLAG_OPTIONS(MAIN_PRINT_FLAG_USAGE)
#undef MAIN_PRINT_FLAG_USAGE

    error2("\n");
    error2("lyrics options:\n");
#define MAIN_PRINT_VALUE_USAGE(id, name, metavar, description, default_text, \
                               apply_fn) \
    main_print_value_option_usage(name, metavar, description, default_text);
    MAIN_LYRICS_VALUE_OPTIONS(MAIN_PRINT_VALUE_USAGE)
#undef MAIN_PRINT_VALUE_USAGE
#define MAIN_PRINT_FLAG_USAGE(id, name, description, apply_fn) \
    main_print_flag_option_usage(name, description);
    MAIN_LYRICS_FLAG_OPTIONS(MAIN_PRINT_FLAG_USAGE)
#undef MAIN_PRINT_FLAG_USAGE

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

#define MAIN_ENUM_VALUE(e, name) {name, (int32)e},
static MainEnumValue main_model_output_values[] = {
    MDX_MODEL_OUTPUT_VALUES(MAIN_ENUM_VALUE)
};
static MainEnumValue main_clip_mode_values[] = {
    MDX_CLIP_MODE_VALUES(MAIN_ENUM_VALUE)
};
static MainEnumValue main_emission_values_kind_values[] = {
    LRC_CTC_EMISSION_VALUES_KIND_VALUES(MAIN_ENUM_VALUE)
};
#undef MAIN_ENUM_VALUE

static bool
main_parse_enum_value(
    char *option,
    char *value,
    MainEnumValue *values,
    int32 value_count,
    int32 *out
) {
    if ((value == NULL) || (out == NULL)) {
        return false;
    }

    for (int32 i = 0; i < value_count; i += 1) {
        if (strequal(value, values[i].name)) {
            *out = values[i].value;
            return true;
        }
    }

    error2("%s must be ", option);
    for (int32 i = 0; i < value_count; i += 1) {
        if (i > 0) {
            error2(", ");
        }
        error2("%s", values[i].name);
    }
    error2("\n");

    return false;
}

static bool
main_parse_vocals_format(LrcPipelineConfig *config, char *option, char *value) {
    if (lrc_audio_format_valid(value)) {
        config->vocals_container_format = value;
        return true;
    }

    error2("%s must be %s\n", option, LRC_AUDIO_FORMAT_NAMES);

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
    if (lrc_audio_format_valid(extension)) {
        config->vocals_container_format = extension;
    }

    return;
}

static bool
main_apply_input_song(MainOptions *options, char *option, char *value) {
    (void)option;

    options->config.song_path = value;

    return true;
}

static bool
main_apply_input_vocals(MainOptions *options, char *option, char *value) {
    (void)option;

    options->config.existing_vocals_path = value;

    return true;
}

static bool
main_apply_output_vocals(MainOptions *options, char *option, char *value) {
    (void)option;

    options->config.vocals_path = value;
    main_infer_vocals_format(&options->config, value);

    return true;
}

static bool
main_apply_input_lyrics(MainOptions *options, char *option, char *value) {
    (void)option;

    options->config.lyrics_text_path = value;

    return true;
}

static bool
main_apply_output_lrc(MainOptions *options, char *option, char *value) {
    (void)option;

    options->config.output_lrc_path = value;

    return true;
}

static bool
main_apply_model_vocal(MainOptions *options, char *option, char *value) {
    (void)option;

    options->config.vocals_model_path = value;

    return true;
}

static bool
main_apply_model_ctc(MainOptions *options, char *option, char *value) {
    (void)option;

    options->config.ctc_model_path = value;

    return true;
}

static bool
main_apply_tokenizer(MainOptions *options, char *option, char *value) {
    (void)option;

    options->config.tokenizer_path = value;

    return true;
}

static bool
main_apply_onnx_provider(MainOptions *options, char *option, char *value) {
    enum OrtExecutionProvider provider;

    if (!ort_execution_provider_parse(value, &provider)) {
        error2("%s must be %s\n", option, ORT_EXECUTION_PROVIDER_NAMES);
        return false;
    }

    options->onnx_provider_set = true;
    options->config.ort_session_config.execution_provider = provider;

    return true;
}

static bool
main_apply_onnx_device(MainOptions *options, char *option, char *value) {
    int32 device_id;

    if (!main_parse_int32(value, option, &device_id)) {
        return false;
    }

    options->onnx_device_set = true;
    options->config.ort_session_config.device_id = device_id;

    return true;
}

static bool
main_apply_ffmpeg(MainOptions *options, char *option, char *value) {
    (void)option;

    options->config.ffmpeg_path = value;

    return true;
}

static bool
main_apply_temp_dir(MainOptions *options, char *option, char *value) {
    (void)option;

    options->config.temp_dir = value;

    return true;
}

static bool
main_apply_vocals_format(MainOptions *options, char *option, char *value) {
    return main_parse_vocals_format(&options->config, option, value);
}

static bool
main_apply_chunk_seconds(MainOptions *options, char *option, char *value) {
    return main_parse_positive_int32(
        value,
        option,
        &options->config.mdx_config.chunk_seconds
    );
}

static bool
main_apply_margin_seconds(MainOptions *options, char *option, char *value) {
    return main_parse_int32(
        value,
        option,
        &options->config.mdx_config.margin_seconds
    );
}

static bool
main_apply_compensate(MainOptions *options, char *option, char *value) {
    return main_parse_float(
        value,
        option,
        &options->config.mdx_config.compensate
    );
}

static bool
main_apply_n_fft(MainOptions *options, char *option, char *value) {
    return main_parse_positive_int32(
        value,
        option,
        &options->config.mdx_config.n_fft
    );
}

static bool
main_apply_hop(MainOptions *options, char *option, char *value) {
    return main_parse_positive_int32(
        value,
        option,
        &options->config.mdx_config.hop
    );
}

static bool
main_apply_dim_f(MainOptions *options, char *option, char *value) {
    return main_parse_positive_int32(
        value,
        option,
        &options->config.mdx_config.dim_f
    );
}

static bool
main_apply_dim_t(MainOptions *options, char *option, char *value) {
    return main_parse_positive_int32(
        value,
        option,
        &options->config.mdx_config.dim_t
    );
}

static bool
main_apply_model_output(MainOptions *options, char *option, char *value) {
    int32 parsed;

    if (!main_parse_enum_value(option,
                               value,
                               main_model_output_values,
                               LENGTH(main_model_output_values),
                               &parsed)) {
        return false;
    }

    options->config.mdx_config.model_output = (enum MdxModelOutput)parsed;

    return true;
}

static bool
main_apply_clip_mode(MainOptions *options, char *option, char *value) {
    int32 parsed;

    if (!main_parse_enum_value(option,
                               value,
                               main_clip_mode_values,
                               LENGTH(main_clip_mode_values),
                               &parsed)) {
        return false;
    }

    options->config.mdx_config.clip_mode = (enum MdxClipMode)parsed;

    return true;
}

static bool
main_apply_ctc_debug_dump(MainOptions *options, char *option, char *value) {
    (void)option;

    options->config.ctc_debug_dump_path = value;

    return true;
}

static bool
main_apply_split_size(MainOptions *options, char *option, char *value) {
    (void)option;

    return lrc_pipeline_parse_preprocess_split_size(&options->config, value);
}

static bool
main_apply_star_frequency(MainOptions *options, char *option, char *value) {
    (void)option;

    return lrc_pipeline_parse_preprocess_star_frequency(&options->config,
                                                        value);
}

static bool
main_apply_romanization(MainOptions *options, char *option, char *value) {
    (void)option;

    return lrc_pipeline_parse_preprocess_romanization(&options->config,
                                                      value);
}

static bool
main_apply_language(MainOptions *options, char *option, char *value) {
    (void)option;

    return lrc_pipeline_parse_preprocess_language(&options->config, value);
}

static bool
main_apply_emissions(MainOptions *options, char *option, char *value) {
    int32 parsed;

    if (!main_parse_enum_value(option,
                               value,
                               main_emission_values_kind_values,
                               LENGTH(main_emission_values_kind_values),
                               &parsed)) {
        return false;
    }

    options->config.ctc_emission_values_kind =
        (enum LrcCtcEmissionValuesKind)parsed;

    return true;
}

static bool
main_apply_help(MainOptions *options) {
    (void)options;

    main_print_usage(stdout);
}

static bool
main_apply_keep_temp_files(MainOptions *options) {
    options->config.keep_temp_files = true;

    return true;
}

static bool
main_apply_romanize(MainOptions *options) {
    lrc_pipeline_enable_preprocess_romanization(&options->config);

    return true;
}

static bool
main_apply_denoise(MainOptions *options) {
    options->config.mdx_config.denoise = true;

    return true;
}

static MainValueOption main_value_options[] = {
#define MAIN_VALUE_OPTION_ENTRY(id, name, metavar, description, default_text, \
                                apply_fn) \
    {name, MAIN_VALUE_OPTION_##id, apply_fn},
    MAIN_VALUE_OPTIONS(MAIN_VALUE_OPTION_ENTRY)
#undef MAIN_VALUE_OPTION_ENTRY
#define MAIN_VALUE_OPTION_ALIAS_ENTRY(id, name, apply_fn) \
    {name, MAIN_VALUE_OPTION_##id, apply_fn},
    MAIN_VALUE_OPTION_ALIASES(MAIN_VALUE_OPTION_ALIAS_ENTRY)
#undef MAIN_VALUE_OPTION_ALIAS_ENTRY
};

static MainFlagOption main_flag_options[] = {
#define MAIN_FLAG_OPTION_ENTRY(id, name, description, apply_fn) \
    {name, MAIN_FLAG_OPTION_##id, apply_fn},
    MAIN_FLAG_OPTIONS(MAIN_FLAG_OPTION_ENTRY)
#undef MAIN_FLAG_OPTION_ENTRY
#define MAIN_FLAG_OPTION_ALIAS_ENTRY(id, name, apply_fn) \
    {name, MAIN_FLAG_OPTION_##id, apply_fn},
    MAIN_FLAG_OPTION_ALIASES(MAIN_FLAG_OPTION_ALIAS_ENTRY)
#undef MAIN_FLAG_OPTION_ALIAS_ENTRY
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

static char *
main_value_option_name(enum MainValueOptionKind kind) {
    for (int32 i = 0; i < LENGTH(main_value_options); i += 1) {
        if (main_value_options[i].kind == kind) {
            return main_value_options[i].name;
        }
    }

    return "unknown option";
}

static bool
main_parse_value_option(MainOptions *options, char *option, char *value) {
    MainValueOption *value_option;

    if ((value_option = main_find_value_option(option)) == NULL) {
        error2("unknown option: %s\n", option);
        return false;
    }

    return value_option->apply(options, option, value);
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
        if (!value_option->apply(options, value_option->name, value)) {
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

static int32
main_parse_flag_option(MainOptions *options, char *option) {
    MainFlagOption *flag_option;

    if ((flag_option = main_find_flag_option(option)) == NULL) {
        return 0;
    }
    if (!flag_option->apply(options)) {
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
        error2("warning: LRC_ONNX_PROVIDER must be %s\n",
               ORT_EXECUTION_PROVIDER_NAMES);
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
        error2("%s and %s cannot both be passed\n",
               main_value_option_name(MAIN_VALUE_OPTION_INPUT_SONG),
               main_value_option_name(MAIN_VALUE_OPTION_INPUT_VOCALS));
        main_mark_usage_error(options);
        return false;
    }
    if (!has_song && !has_vocals) {
        error2("missing required option: %s or %s\n",
               main_value_option_name(MAIN_VALUE_OPTION_INPUT_SONG),
               main_value_option_name(MAIN_VALUE_OPTION_INPUT_VOCALS));
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
        error2("missing LRC output path: %s\n",
               main_value_option_name(MAIN_VALUE_OPTION_OUTPUT_LRC));
        main_mark_usage_error(options);
        return false;
    }
    if (has_song && path_missing(config->vocals_model_path)) {
        error2("missing required option: %s\n",
               main_value_option_name(MAIN_VALUE_OPTION_MODEL_VOCAL));
        main_mark_usage_error(options);
        return false;
    }
    if (has_lyrics && path_missing(config->ctc_model_path)) {
        error2("missing required option: %s\n",
               main_value_option_name(MAIN_VALUE_OPTION_MODEL_CTC));
        main_mark_usage_error(options);
        return false;
    }
    if (has_lyrics && path_missing(config->tokenizer_path)) {
        error2("missing required option: %s\n",
               main_value_option_name(MAIN_VALUE_OPTION_TOKENIZER));
        main_mark_usage_error(options);
        return false;
    }
    if (config->mdx_config.margin_seconds < 0) {
        error2("%s must not be negative\n",
               main_value_option_name(MAIN_VALUE_OPTION_MARGIN_SECONDS));
        main_mark_usage_error(options);
        return false;
    }
    if (config->ort_session_config.device_id < 0) {
        error2("%s must not be negative\n",
               main_value_option_name(MAIN_VALUE_OPTION_ONNX_DEVICE));
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
            result->error = LS_ERROR_VOCALS_EXTRACT_INVALID_ARGUMENT;
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
            result->error = LS_ERROR_PIPELINE_GENERATE_INVALID_ARGUMENT;
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
    MainOptions options = {0};
    bool has_lyrics;

    if ((argc > 0) && argv) {
        program = argv[0];
    }

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
