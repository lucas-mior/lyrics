#include "cbase.h"
#include "cli.h"

#if !defined(TESTING_cli)
#define TESTING_cli 0
#endif

static bool
long_option_value(char *arg, char *name, char **value) {
    int32 i;

    for (i = 0; name[i] != '\0'; i += 1) {
        if (arg[i] != name[i]) {
            return false;
        }
    }

    if (arg[i] != '=') {
        return false;
    }

    *value = &arg[i + 1];

    return true;
}

static bool
is_value_option(char *arg) {
    return strequal(arg, "-i")
           || strequal(arg, "--input")
           || strequal(arg, "-o")
           || strequal(arg, "--output")
           || strequal(arg, "-m")
           || strequal(arg, "--model")
           || strequal(arg, "--ffmpeg")
           || strequal(arg, "--format")
           || strequal(arg, "--chunk-seconds")
           || strequal(arg, "--margin-seconds")
           || strequal(arg, "--n-fft")
           || strequal(arg, "--hop")
           || strequal(arg, "--dim-f")
           || strequal(arg, "--dim-t")
           || strequal(arg, "--compensate")
           || strequal(arg, "--model-output")
           || strequal(arg, "--clip-mode");
}

static bool
needs_value(int32 i, int32 argc, char *option) {
    if (i + 1 < argc) {
        return true;
    }

    error2("%s requires a value\n", option);

    return false;
}

static int32
parse_int32(char *value, char *name, int32 *out) {
    char *end;
    int64 result;

    errno = 0;
    end = NULL;
    result = strtoll(value, &end, 10);
    if ((errno != 0) || (end == value) || (*end != '\0')) {
        error2("%s must be an integer: %s\n", name, value);
        return -1;
    }
    if ((result < 0) || (result > INT32_MAX)) {
        error2("%s is outside the supported range: %s\n", name,
                value);
        return -1;
    }

    *out = (int32)result;

    return 0;
}

static int32
parse_positive_int32(char *value, char *name, int32 *out) {
    if (parse_int32(value, name, out) != 0) {
        return -1;
    }
    if (*out <= 0) {
        error2("%s must be greater than zero: %s\n", name, value);
        return -1;
    }

    return 0;
}

static int32
parse_nonnegative_int32(char *value, char *name, int32 *out) {
    if (parse_int32(value, name, out) != 0) {
        return -1;
    }

    return 0;
}

static int32
parse_float(char *value, char *name, float *out) {
    char *end;
    float result;

    errno = 0;
    end = NULL;
    result = strtof(value, &end);
    if ((errno != 0) || (end == value) || (*end != '\0')) {
        error2("%s must be a number: %s\n", name, value);
        return -1;
    }
    if (result < 0.0f) {
        error2("%s must not be negative: %s\n", name, value);
        return -1;
    }

    *out = result;

    return 0;
}

static int32
parse_format(CliOptions *options, char *value) {
    if (strequal(value, "wav")
        || strequal(value, "flac")
        || strequal(value, "mp3")) {
        options->format = value;
        return 0;
    }

    error2("--format must be one of: wav, flac, mp3\n");

    return -1;
}

static int32
parse_model_output(CliOptions *options, char *value) {
    if (strequal(value, "vocals")) {
        options->model_output = CLI_MODEL_OUTPUT_VOCALS;
        return 0;
    }
    if (strequal(value, "instrumental")) {
        options->model_output = CLI_MODEL_OUTPUT_INSTRUMENTAL;
        return 0;
    }

    error2("--model-output must be one of: vocals, instrumental\n");

    return -1;
}

static int32
parse_clip_mode(CliOptions *options, char *value) {
    if (strequal(value, "clamp")) {
        options->clip_mode = CLI_CLIP_MODE_CLAMP;
        return 0;
    }
    if (strequal(value, "none")) {
        options->clip_mode = CLI_CLIP_MODE_NONE;
        return 0;
    }

    error2("--clip-mode must be one of: clamp, none\n");

    return -1;
}

static char *
model_output_string(enum CliModelOutput output) {
    char *result;

    switch (output) {
    case CLI_MODEL_OUTPUT_VOCALS:
        result = "vocals";
        break;
    case CLI_MODEL_OUTPUT_INSTRUMENTAL:
        result = "instrumental";
        break;
    default:
        result = "unknown";
        break;
    }

    return result;
}

static char *
clip_mode_string(enum CliClipMode mode) {
    char *result;

    switch (mode) {
    case CLI_CLIP_MODE_CLAMP:
        result = "clamp";
        break;
    case CLI_CLIP_MODE_NONE:
        result = "none";
        break;
    default:
        result = "unknown";
        break;
    }

    return result;
}

static char *
string_or_empty(char *value) {
    if (value) {
        return value;
    }

    return "";
}

static int32
parse_value_option(CliOptions *options, char *arg, char *value) {
    if (strequal(arg, "-i")
        || strequal(arg, "--input")) {
        options->input_path = value;
        return 0;
    }
    if (strequal(arg, "-o")
        || strequal(arg, "--output")) {
        options->output_path = value;
        return 0;
    }
    if (strequal(arg, "-m")
        || strequal(arg, "--model")) {
        options->model_path = value;
        return 0;
    }
    if (strequal(arg, "--ffmpeg")) {
        options->ffmpeg_path = value;
        return 0;
    }
    if (strequal(arg, "--format")) {
        return parse_format(options, value);
    }
    if (strequal(arg, "--chunk-seconds")) {
        return parse_positive_int32(value, arg, &options->chunk_seconds);
    }
    if (strequal(arg, "--margin-seconds")) {
        return parse_nonnegative_int32(value, arg, &options->margin_seconds);
    }
    if (strequal(arg, "--n-fft")) {
        return parse_positive_int32(value, arg, &options->n_fft);
    }
    if (strequal(arg, "--hop")) {
        return parse_positive_int32(value, arg, &options->hop);
    }
    if (strequal(arg, "--dim-f")) {
        return parse_positive_int32(value, arg, &options->dim_f);
    }
    if (strequal(arg, "--dim-t")) {
        return parse_positive_int32(value, arg, &options->dim_t);
    }
    if (strequal(arg, "--compensate")) {
        return parse_float(value, arg, &options->compensate);
    }
    if (strequal(arg, "--model-output")) {
        return parse_model_output(options, value);
    }
    if (strequal(arg, "--clip-mode")) {
        return parse_clip_mode(options, value);
    }

    return 1;
}

static int32
parse_long_value_option(CliOptions *options, char *arg) {
    char *value;

    value = NULL;
    if (long_option_value(arg, "--input", &value)) {
        options->input_path = value;
        return 0;
    }
    if (long_option_value(arg, "--output", &value)) {
        options->output_path = value;
        return 0;
    }
    if (long_option_value(arg, "--model", &value)) {
        options->model_path = value;
        return 0;
    }
    if (long_option_value(arg, "--ffmpeg", &value)) {
        options->ffmpeg_path = value;
        return 0;
    }
    if (long_option_value(arg, "--format", &value)) {
        return parse_format(options, value);
    }
    if (long_option_value(arg, "--chunk-seconds", &value)) {
        return parse_positive_int32(value, "--chunk-seconds",
                                    &options->chunk_seconds);
    }
    if (long_option_value(arg, "--margin-seconds", &value)) {
        return parse_nonnegative_int32(value, "--margin-seconds",
                                       &options->margin_seconds);
    }
    if (long_option_value(arg, "--n-fft", &value)) {
        return parse_positive_int32(value, "--n-fft", &options->n_fft);
    }
    if (long_option_value(arg, "--hop", &value)) {
        return parse_positive_int32(value, "--hop", &options->hop);
    }
    if (long_option_value(arg, "--dim-f", &value)) {
        return parse_positive_int32(value, "--dim-f", &options->dim_f);
    }
    if (long_option_value(arg, "--dim-t", &value)) {
        return parse_positive_int32(value, "--dim-t", &options->dim_t);
    }
    if (long_option_value(arg, "--compensate", &value)) {
        return parse_float(value, "--compensate", &options->compensate);
    }
    if (long_option_value(arg, "--model-output", &value)) {
        return parse_model_output(options, value);
    }
    if (long_option_value(arg, "--clip-mode", &value)) {
        return parse_clip_mode(options, value);
    }

    return 1;
}

static int32
validate_options(CliOptions *options) {
    if (options->input_path == NULL) {
        error2("missing required option: -i/--input\n");
        return -1;
    }
    if (options->output_path == NULL) {
        error2("missing required option: -o/--output\n");
        return -1;
    }
    if (options->model_path == NULL) {
        error2("missing required option: -m/--model\n");
        return -1;
    }

    return 0;
}

static void
cli_options_init(CliOptions *options) {
    options->input_path = NULL;
    options->output_path = NULL;
    options->model_path = NULL;
    options->ffmpeg_path = "ffmpeg";
    options->format = "wav";

    options->chunk_seconds = 30;
    options->margin_seconds = 3;
    options->n_fft = 6144;
    options->hop = 1024;
    options->dim_f = 0;
    options->dim_t = 0;

    options->compensate = 1.035f;
    options->denoise = false;

    options->model_output = CLI_MODEL_OUTPUT_VOCALS;
    options->clip_mode = CLI_CLIP_MODE_CLAMP;

    return;
}

static int32
cli_parse(CliOptions *options, int32 argc, char **argv) {
    int32 i;
    int32 parsed;

    for (i = 1; i < argc; i += 1) {
        if (strequal(argv[i], "-h")
            || strequal(argv[i], "--help")) {
            cli_print_usage(stdout);
        }
        if (strequal(argv[i], "--denoise")) {
            options->denoise = true;
            continue;
        }

        parsed = parse_long_value_option(options, argv[i]);
        if (parsed == 0) {
            continue;
        }
        if (parsed < 0) {
            return -1;
        }

        if (!is_value_option(argv[i])) {
            error2("unknown option: %s\n", argv[i]);
            return -1;
        }
        if (!needs_value(i, argc, argv[i])) {
            return -1;
        }

        parsed = parse_value_option(options, argv[i], argv[i + 1]);
        if (parsed < 0) {
            return -1;
        }

        i += 1;
    }

    return validate_options(options);
}

static void
cli_print_usage(FILE *stream) {
    error2(
        "usage: %s -i INPUT -o OUTPUT -m MODEL [options]\n"
        "\n"
        "required:\n"
        "    -i, --input PATH             music file to read\n"
        "    -o, --output PATH            vocals file to write\n"
        "    -m, --model PATH             MDX-Net ONNX model\n"
        "\n"
        "options:\n"
        "    --ffmpeg PATH                ffmpeg executable [ffmpeg]\n"
        "    --format wav|flac|mp3        output format [wav]\n"
        "    --chunk-seconds N            chunk size in seconds [30]\n"
        "    --margin-seconds N           chunk margin in seconds [3]\n"
        "    --denoise                    run denoising inference mode\n"
        "    --compensate X               output gain [1.035]\n"
        "    --n-fft N                    STFT size [6144]\n"
        "    --hop N                      STFT hop [1024]\n"
        "    --dim-f N                    override model frequency bins\n"
        "    --dim-t N                    override model time frames\n"
        "    --model-output vocals|instrumental [vocals]\n"
        "    --clip-mode clamp|none       final clipping policy [clamp]\n",
        program);
    exit(stream == stdin);
}

static void
cli_print_options(CliOptions *options) {
    printf("input: %s\n", string_or_empty(options->input_path));
    printf("output: %s\n", string_or_empty(options->output_path));
    printf("model: %s\n", string_or_empty(options->model_path));
    printf("ffmpeg: %s\n", options->ffmpeg_path);
    printf("format: %s\n", options->format);
    printf("chunk_seconds: %d\n", options->chunk_seconds);
    printf("margin_seconds: %d\n", options->margin_seconds);
    if (options->denoise) {
        printf("denoise: true\n");
    } else {
        printf("denoise: false\n");
    }
    printf("compensate: %.9g\n", (double)options->compensate);
    printf("n_fft: %d\n", options->n_fft);
    printf("hop: %d\n", options->hop);
    printf("dim_f: %d\n", options->dim_f);
    printf("dim_t: %d\n", options->dim_t);
    printf("model_output: %s\n", model_output_string(options->model_output));
    printf("clip_mode: %s\n", clip_mode_string(options->clip_mode));

    return;
}

#if TESTING_cli

#define CBASE_IMPLEMENT
#include "cbase.h"

static int32
cli_test_fail(char *name) {
    error2("cli test failed: %s\n", name);

    return 1;
}

static int32
cli_test_successful_parse(void) {
    CliOptions options;
    char *argv[] = {
        "uvr-c",
        "-i",
        "song.mp3",
        "--output=voice.flac",
        "-m",
        "models/Kim_Vocal_2.onnx",
        "--ffmpeg",
        "ffmpeg-custom",
        "--format",
        "flac",
        "--chunk-seconds=45",
        "--margin-seconds",
        "0",
        "--denoise",
        "--compensate",
        "1.5",
        "--n-fft",
        "4096",
        "--hop=512",
        "--dim-f",
        "2048",
        "--dim-t=256",
        "--model-output",
        "instrumental",
        "--clip-mode=none",
    };
    int32 argc;

    argc = (int32)LENGTH(argv);
    cli_options_init(&options);
    if (cli_parse(&options, argc, argv) != 0) {
        return cli_test_fail("valid command line did not parse");
    }
    if (!strequal(options.input_path, "song.mp3")) {
        return cli_test_fail("input path");
    }
    if (!strequal(options.output_path, "voice.flac")) {
        return cli_test_fail("output path");
    }
    if (!strequal(options.model_path, "models/Kim_Vocal_2.onnx")) {
        return cli_test_fail("model path");
    }
    if (!strequal(options.ffmpeg_path, "ffmpeg-custom")) {
        return cli_test_fail("ffmpeg path");
    }
    if (!strequal(options.format, "flac")) {
        return cli_test_fail("format");
    }
    if (options.chunk_seconds != 45) {
        return cli_test_fail("chunk seconds");
    }
    if (options.margin_seconds != 0) {
        return cli_test_fail("margin seconds");
    }
    if (!options.denoise) {
        return cli_test_fail("denoise");
    }
    if (options.compensate != 1.5f) {
        return cli_test_fail("compensate");
    }
    if (options.n_fft != 4096) {
        return cli_test_fail("n_fft");
    }
    if (options.hop != 512) {
        return cli_test_fail("hop");
    }
    if (options.dim_f != 2048) {
        return cli_test_fail("dim_f");
    }
    if (options.dim_t != 256) {
        return cli_test_fail("dim_t");
    }
    if (options.model_output != CLI_MODEL_OUTPUT_INSTRUMENTAL) {
        return cli_test_fail("model output");
    }
    if (options.clip_mode != CLI_CLIP_MODE_NONE) {
        return cli_test_fail("clip mode");
    }

    return 0;
}

static int32
cli_test_default_parse(void) {
    CliOptions options;
    char *argv[] = {
        "uvr-c",
        "-i",
        "song.mp3",
        "-o",
        "voice.wav",
        "-m",
        "model.onnx",
    };
    int32 argc;

    argc = (int32)LENGTH(argv);
    cli_options_init(&options);
    if (cli_parse(&options, argc, argv) != 0) {
        return cli_test_fail("default command line did not parse");
    }
    if (!strequal(options.ffmpeg_path, "ffmpeg")) {
        return cli_test_fail("default ffmpeg");
    }
    if (!strequal(options.format, "wav")) {
        return cli_test_fail("default format");
    }
    if (options.chunk_seconds != 30) {
        return cli_test_fail("default chunk seconds");
    }
    if (options.margin_seconds != 3) {
        return cli_test_fail("default margin seconds");
    }
    if (options.n_fft != 6144) {
        return cli_test_fail("default n_fft");
    }
    if (options.hop != 1024) {
        return cli_test_fail("default hop");
    }
    if (options.dim_f != 0) {
        return cli_test_fail("default dim_f");
    }
    if (options.dim_t != 0) {
        return cli_test_fail("default dim_t");
    }
    if (options.compensate != 1.035f) {
        return cli_test_fail("default compensate");
    }
    if (options.denoise) {
        return cli_test_fail("default denoise");
    }
    if (options.model_output != CLI_MODEL_OUTPUT_VOCALS) {
        return cli_test_fail("default model output");
    }
    if (options.clip_mode != CLI_CLIP_MODE_CLAMP) {
        return cli_test_fail("default clip mode");
    }

    return 0;
}

static int32
cli_test_reject_missing_required(void) {
    CliOptions options;
    char *argv[] = {
        "uvr-c",
        "-i",
        "song.mp3",
        "-o",
        "voice.wav",
    };
    int32 argc;

    argc = (int32)LENGTH(argv);
    cli_options_init(&options);
    if (cli_parse(&options, argc, argv) == 0) {
        return cli_test_fail("missing model accepted");
    }

    return 0;
}

static int32
cli_test_reject_invalid_value(void) {
    CliOptions options;
    char *argv[] = {
        "uvr-c",
        "-i",
        "song.mp3",
        "-o",
        "voice.wav",
        "-m",
        "model.onnx",
        "--model-output",
        "drums",
    };
    int32 argc;

    argc = (int32)LENGTH(argv);
    cli_options_init(&options);
    if (cli_parse(&options, argc, argv) == 0) {
        return cli_test_fail("invalid model output accepted");
    }

    return 0;
}

int
main(void) {
    if (cli_test_successful_parse() != 0) {
        exit(1);
    }
    if (cli_test_default_parse() != 0) {
        exit(1);
    }
    if (cli_test_reject_missing_required() != 0) {
        exit(1);
    }
    if (cli_test_reject_invalid_value() != 0) {
        exit(1);
    }

    exit(0);
}

#endif /* TESTING_cli */
