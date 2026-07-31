#if !defined(PIPELINE_H)
#define PIPELINE_H

#include "cbase.h"
#include "audio.h"
#include "mdx.h"
#include "vocals.h"

enum LrcPipelineError {
    LRC_PIPELINE_ERROR_NONE,
    LRC_PIPELINE_ERROR_INVALID_ARGUMENT,
    LRC_PIPELINE_ERROR_TEMP_DIR_MISSING,
    LRC_PIPELINE_ERROR_TEMP_PATH_TOO_LONG,
    LRC_PIPELINE_ERROR_TEMP_DIR_CREATE_FAILED,
    LRC_PIPELINE_ERROR_TEMP_CLEANUP_FAILED,
    LRC_PIPELINE_ERROR_VOCALS_ALREADY_AVAILABLE,
    LRC_PIPELINE_ERROR_VOCALS_EXTRACT_FAILED,
};

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

#endif /* PIPELINE_H */
