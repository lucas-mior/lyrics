#if !defined(VOCALS_H)
#define VOCALS_H

#include "cbase.h"
#include "audio.h"
#include "mdx.h"

/*
 * Phase-1 public API: original song + MDX model -> extracted vocals file.
 */
enum LrcVocalsExtractError {
    LRC_VOCALS_EXTRACT_ERROR_NONE,
    LRC_VOCALS_EXTRACT_ERROR_INVALID_ARGUMENT,
    LRC_VOCALS_EXTRACT_ERROR_MISSING_INPUT,
    LRC_VOCALS_EXTRACT_ERROR_MISSING_OUTPUT,
    LRC_VOCALS_EXTRACT_ERROR_MISSING_MODEL,
    LRC_VOCALS_EXTRACT_ERROR_MISSING_TEMP_DIR,
    LRC_VOCALS_EXTRACT_ERROR_MISSING_FFMPEG,
    LRC_VOCALS_EXTRACT_ERROR_FFMPEG_UNAVAILABLE,
    LRC_VOCALS_EXTRACT_ERROR_INPUT_DECODE_FAILED,
    LRC_VOCALS_EXTRACT_ERROR_MODEL_OPEN_FAILED,
    LRC_VOCALS_EXTRACT_ERROR_STFT_INIT_FAILED,
    LRC_VOCALS_EXTRACT_ERROR_ORT_INIT_FAILED,
    LRC_VOCALS_EXTRACT_ERROR_ORT_MODEL_LOAD_FAILED,
    LRC_VOCALS_EXTRACT_ERROR_UNSUPPORTED_MDX_MODEL,
    LRC_VOCALS_EXTRACT_ERROR_MDX_CONFIG_FAILED,
    LRC_VOCALS_EXTRACT_ERROR_INPUT_READ_FAILED,
    LRC_VOCALS_EXTRACT_ERROR_MDX_PROCESS_FAILED,
    LRC_VOCALS_EXTRACT_ERROR_OUTPUT_WRITE_FAILED,
};

typedef struct LrcVocalsExtractRequest {
    char *input_path;
    char *output_path;
    char *model_path;
    char *temp_dir;
    char *ffmpeg_path;
    char *container_format;

    AudioIoFormat output_format;
    MdxConfig mdx_config;

    bool print_info;
} LrcVocalsExtractRequest;

typedef struct LrcVocalsExtractResult {
    enum LrcVocalsExtractError error;
    char *message;
    char *path;
} LrcVocalsExtractResult;

static void lrc_vocals_extract_request_init(LrcVocalsExtractRequest *request);
static void lrc_vocals_extract_result_init(LrcVocalsExtractResult *result);
static bool lrc_extract_vocals(
    LrcVocalsExtractRequest *request,
    LrcVocalsExtractResult *result
);

#endif /* VOCALS_H */
