#if !defined(PIPELINE_H)
#define PIPELINE_H

#include "cbase.h"
#include "audio.h"
#include "mdx.h"
#include "vocals.h"
#include "ctc_assets.h"
#include "lyrics.h"
#include "ctc_tokenizer.h"
#include "ctc_audio.h"
#include "ctc_model.h"
#include "ctc_inference.h"
#include "ctc_align.h"
#include "lrc.h"

enum LrcPipelineError {
    LRC_PIPELINE_ERROR_NONE,
    LRC_PIPELINE_ERROR_INVALID_ARGUMENT,
    LRC_PIPELINE_ERROR_TEMP_DIR_MISSING,
    LRC_PIPELINE_ERROR_TEMP_PATH_TOO_LONG,
    LRC_PIPELINE_ERROR_TEMP_DIR_CREATE_FAILED,
    LRC_PIPELINE_ERROR_TEMP_CLEANUP_FAILED,
    LRC_PIPELINE_ERROR_VOCALS_ALREADY_AVAILABLE,
    LRC_PIPELINE_ERROR_VOCALS_EXTRACT_FAILED,
    LRC_PIPELINE_ERROR_CTC_ASSETS_INVALID,
    LRC_PIPELINE_ERROR_GENERATE_FAILED,
};

enum LrcPipelineGenerateError {
    LRC_PIPELINE_GENERATE_ERROR_NONE,
    LRC_PIPELINE_GENERATE_ERROR_INVALID_ARGUMENT,
    LRC_PIPELINE_GENERATE_ERROR_MISSING_SONG,
    LRC_PIPELINE_GENERATE_ERROR_MISSING_LYRICS,
    LRC_PIPELINE_GENERATE_ERROR_MISSING_OUTPUT,
    LRC_PIPELINE_GENERATE_ERROR_MISSING_VOCALS_MODEL,
    LRC_PIPELINE_GENERATE_ERROR_MISSING_CTC_MODEL,
    LRC_PIPELINE_GENERATE_ERROR_MISSING_TOKENIZER,
    LRC_PIPELINE_GENERATE_ERROR_PREPARE_FAILED,
    LRC_PIPELINE_GENERATE_ERROR_VOCALS_EXTRACT_FAILED,
    LRC_PIPELINE_GENERATE_ERROR_CTC_ASSETS_INVALID,
    LRC_PIPELINE_GENERATE_ERROR_LYRICS_LOAD_FAILED,
    LRC_PIPELINE_GENERATE_ERROR_LYRICS_NORMALIZE_FAILED,
    LRC_PIPELINE_GENERATE_ERROR_TOKENIZER_LOAD_FAILED,
    LRC_PIPELINE_GENERATE_ERROR_TOKENIZE_FAILED,
    LRC_PIPELINE_GENERATE_ERROR_AUDIO_DECODE_FAILED,
    LRC_PIPELINE_GENERATE_ERROR_MODEL_INPUT_FAILED,
    LRC_PIPELINE_GENERATE_ERROR_CTC_MODEL_LOAD_FAILED,
    LRC_PIPELINE_GENERATE_ERROR_CTC_INFERENCE_FAILED,
    LRC_PIPELINE_GENERATE_ERROR_EMISSION_CONVERSION_FAILED,
    LRC_PIPELINE_GENERATE_ERROR_ALIGNMENT_FAILED,
    LRC_PIPELINE_GENERATE_ERROR_OUTPUT_LINES_FAILED,
    LRC_PIPELINE_GENERATE_ERROR_LRC_WRITE_FAILED,
    LRC_PIPELINE_GENERATE_ERROR_TOO_LARGE,
};

typedef struct LrcPipelineGenerateResult {
    enum LrcPipelineGenerateError error;
    char *message;
    char *path;

    int64 frame_index;
    int64 token_index;
    int32 line_index;
} LrcPipelineGenerateResult;

typedef struct LrcPipelineConfig {
    char *song_path;
    char *lyrics_text_path;
    char *existing_vocals_path;
    char *vocals_path;
    char *output_lrc_path;

    char *vocals_model_path;
    char *ctc_model_path;
    char *tokenizer_path;

    char *temp_dir;
    char *ffmpeg_path;
    char *vocals_container_format;

    AudioIoFormat vocals_output_format;
    MdxConfig mdx_config;
    LrcCtcModelConfig ctc_model_config;

    enum LrcCtcEmissionValuesKind ctc_emission_values_kind;

    bool keep_temp_files;
    bool print_info;
} LrcPipelineConfig;

typedef struct LrcPipeline {
    LrcPipelineConfig config;

    enum LrcPipelineError error;
    char *message;
    char *path;

    char owned_temp_dir[PATH_MAX];
    char owned_vocals_path[PATH_MAX];
    char *vocals_stage_path;

    LrcCtcAssets ctc_assets;

    bool prepared;
    bool owns_temp_dir;
    bool owns_vocals_path;
} LrcPipeline;

static void lrc_pipeline_config_init(LrcPipelineConfig *config);
static void lrc_pipeline_init(
    LrcPipeline *pipeline,
    LrcPipelineConfig *config
);
static bool lrc_pipeline_prepare(LrcPipeline *pipeline);
static void lrc_pipeline_cleanup(LrcPipeline *pipeline);
static bool lrc_pipeline_vocals_request(
    LrcPipeline *pipeline,
    LrcVocalsExtractRequest *request
);
static bool lrc_pipeline_extract_vocals(
    LrcPipeline *pipeline,
    LrcVocalsExtractResult *result
);
static void lrc_pipeline_ctc_assets_config(
    LrcPipeline *pipeline,
    LrcCtcAssetsConfig *config
);
static bool lrc_pipeline_validate_ctc_assets(
    LrcPipeline *pipeline,
    LrcCtcAssetsResult *result
);
static void lrc_pipeline_generate_result_init(
    LrcPipelineGenerateResult *result
);
static bool lrc_pipeline_generate_lrc(
    LrcPipeline *pipeline,
    LrcPipelineGenerateResult *result
);
static bool lrc_generate_from_song(
    LrcPipelineConfig *config,
    LrcPipelineGenerateResult *result
);

#endif /* PIPELINE_H */
