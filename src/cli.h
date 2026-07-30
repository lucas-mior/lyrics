#if !defined(CLI_H)
#define CLI_H

#include "cbase.h"

enum CliModelOutput {
    CLI_MODEL_OUTPUT_VOCALS,
    CLI_MODEL_OUTPUT_INSTRUMENTAL,
};

enum CliClipMode {
    CLI_CLIP_MODE_CLAMP,
    CLI_CLIP_MODE_NONE,
};

typedef struct CliOptions {
    char *input_path;
    char *output_path;
    char *model_path;
    char *ffmpeg_path;
    char *format;

    int32 chunk_seconds;
    int32 margin_seconds;
    int32 n_fft;
    int32 hop;
    int32 dim_f;
    int32 dim_t;

    float compensate;
    bool denoise;

    enum CliModelOutput model_output;
    enum CliClipMode clip_mode;
} CliOptions;

static void cli_options_init(CliOptions *options);
static int32 cli_parse(CliOptions *options, int32 argc, char **argv);
static void cli_print_usage(char *program);
static void cli_print_options(CliOptions *options);

#endif /* CLI_H */
