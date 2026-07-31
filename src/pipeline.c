#include "cbase.h"
#include "pipeline.h"

#if !defined(TESTING_pipeline)
#define TESTING_pipeline 0
#endif

static void
lrc_pipeline_error_set(
    LrcPipeline *pipeline,
    enum LrcPipelineError error,
    char *message,
    char *path
) {
    if (pipeline == NULL) {
        return;
    }

    pipeline->error = error;
    pipeline->message = message;
    pipeline->path = path;

    return;
}

static bool
lrc_pipeline_path_missing(char *path) {
    if (path == NULL) {
        return true;
    }
    if (path[0] == '\0') {
        return true;
    }

    return false;
}

static bool
lrc_pipeline_store_owned_temp_dir(LrcPipeline *pipeline) {
    int32 len;

    if (lrc_pipeline_path_missing(pipeline->config.temp_dir)) {
        lrc_pipeline_error_set(
            pipeline,
            LRC_PIPELINE_ERROR_TEMP_DIR_MISSING,
            "temporary directory path is missing",
            pipeline->config.temp_dir
        );
        return false;
    }

    len = snprintf2(pipeline->owned_temp_dir,
                    SIZEOF(pipeline->owned_temp_dir),
                    "%s/lrc_gen_XXXXXX",
                    pipeline->config.temp_dir);
    if ((len <= 0) || (len >= SIZEOF(pipeline->owned_temp_dir))) {
        lrc_pipeline_error_set(
            pipeline,
            LRC_PIPELINE_ERROR_TEMP_PATH_TOO_LONG,
            "temporary directory path is too long",
            pipeline->config.temp_dir
        );
        return false;
    }

    if (mkdtemp(pipeline->owned_temp_dir) == NULL) {
        lrc_pipeline_error_set(
            pipeline,
            LRC_PIPELINE_ERROR_TEMP_DIR_CREATE_FAILED,
            "could not create temporary directory",
            pipeline->owned_temp_dir
        );
        return false;
    }

    pipeline->owns_temp_dir = true;

    return true;
}

static bool
lrc_pipeline_store_owned_vocals_path(LrcPipeline *pipeline) {
    int32 len;

    if (!pipeline->owns_temp_dir) {
        if (!lrc_pipeline_store_owned_temp_dir(pipeline)) {
            return false;
        }
    }

    len = snprintf2(pipeline->owned_vocals_path,
                    SIZEOF(pipeline->owned_vocals_path),
                    "%s/vocals.wav",
                    pipeline->owned_temp_dir);
    if ((len <= 0) || (len >= SIZEOF(pipeline->owned_vocals_path))) {
        lrc_pipeline_error_set(
            pipeline,
            LRC_PIPELINE_ERROR_TEMP_PATH_TOO_LONG,
            "temporary vocals path is too long",
            pipeline->owned_temp_dir
        );
        return false;
    }

    pipeline->vocals_stage_path = pipeline->owned_vocals_path;
    pipeline->owns_vocals_path = true;

    return true;
}

static bool
lrc_pipeline_prepare_vocals_path(LrcPipeline *pipeline) {
    if (!lrc_pipeline_path_missing(pipeline->config.existing_vocals_path)) {
        pipeline->vocals_stage_path = pipeline->config.existing_vocals_path;
        return true;
    }

    if (!lrc_pipeline_path_missing(pipeline->config.vocals_path)) {
        pipeline->vocals_stage_path = pipeline->config.vocals_path;
        return true;
    }

    return lrc_pipeline_store_owned_vocals_path(pipeline);
}

static void
lrc_pipeline_config_init(LrcPipelineConfig *config) {
    if (config == NULL) {
        return;
    }

    memset64(config, 0, SIZEOF(*config));

    config->temp_dir = "/tmp";
    config->ffmpeg_path = "ffmpeg";
    config->vocals_container_format = "wav";
    config->print_info = true;

    audio_io_format_init(&config->vocals_output_format);
    mdx_config_init(&config->mdx_config);

    return;
}

static void
lrc_pipeline_init(LrcPipeline *pipeline, LrcPipelineConfig *config) {
    if (pipeline == NULL) {
        return;
    }

    memset64(pipeline, 0, SIZEOF(*pipeline));
    lrc_pipeline_error_set(pipeline,
                           LRC_PIPELINE_ERROR_NONE,
                           "ok",
                           NULL);

    if (config) {
        pipeline->config = *config;
    } else {
        lrc_pipeline_config_init(&pipeline->config);
    }

    return;
}

static bool
lrc_pipeline_prepare(LrcPipeline *pipeline) {
    if (pipeline == NULL) {
        return false;
    }

    if (pipeline->prepared) {
        return true;
    }

    lrc_pipeline_error_set(pipeline,
                           LRC_PIPELINE_ERROR_NONE,
                           "ok",
                           NULL);

    if (!lrc_pipeline_prepare_vocals_path(pipeline)) {
        return false;
    }

    pipeline->prepared = true;

    return true;
}

static void
lrc_pipeline_cleanup(LrcPipeline *pipeline) {
    if (pipeline == NULL) {
        return;
    }

    if (pipeline->config.keep_temp_files) {
        return;
    }

    if (pipeline->owns_vocals_path
        && !lrc_pipeline_path_missing(pipeline->owned_vocals_path)) {
        if ((unlink(pipeline->owned_vocals_path) < 0) && (errno != ENOENT)) {
            lrc_pipeline_error_set(
                pipeline,
                LRC_PIPELINE_ERROR_TEMP_CLEANUP_FAILED,
                "could not remove temporary vocals file",
                pipeline->owned_vocals_path
            );
        }
    }

    if (pipeline->owns_temp_dir
        && !lrc_pipeline_path_missing(pipeline->owned_temp_dir)) {
        if ((rmdir(pipeline->owned_temp_dir) < 0) && (errno != ENOENT)) {
            lrc_pipeline_error_set(
                pipeline,
                LRC_PIPELINE_ERROR_TEMP_CLEANUP_FAILED,
                "could not remove temporary directory",
                pipeline->owned_temp_dir
            );
        }
    }

    pipeline->prepared = false;
    pipeline->owns_temp_dir = false;
    pipeline->owns_vocals_path = false;
    pipeline->vocals_stage_path = NULL;
    pipeline->owned_temp_dir[0] = '\0';
    pipeline->owned_vocals_path[0] = '\0';

    return;
}

static bool
lrc_pipeline_vocals_request(
    LrcPipeline *pipeline,
    LrcVocalsExtractRequest *request
) {
    if ((pipeline == NULL) || (request == NULL)) {
        if (pipeline) {
            lrc_pipeline_error_set(
                pipeline,
                LRC_PIPELINE_ERROR_INVALID_ARGUMENT,
                "pipeline vocals request received invalid arguments",
                NULL
            );
        }
        return false;
    }

    if (!lrc_pipeline_prepare(pipeline)) {
        return false;
    }

    if (!lrc_pipeline_path_missing(pipeline->config.existing_vocals_path)) {
        lrc_pipeline_error_set(
            pipeline,
            LRC_PIPELINE_ERROR_VOCALS_ALREADY_AVAILABLE,
            "pipeline already has an extracted vocals path",
            pipeline->config.existing_vocals_path
        );
        return false;
    }

    lrc_vocals_extract_request_init(request);
    request->input_path = pipeline->config.song_path;
    request->output_path = pipeline->vocals_stage_path;
    request->model_path = pipeline->config.vocals_model_path;
    request->temp_dir = pipeline->config.temp_dir;
    request->ffmpeg_path = pipeline->config.ffmpeg_path;
    request->container_format = pipeline->config.vocals_container_format;
    request->output_format = pipeline->config.vocals_output_format;
    request->mdx_config = pipeline->config.mdx_config;
    request->print_info = pipeline->config.print_info;

    return true;
}

static bool
lrc_pipeline_extract_vocals(
    LrcPipeline *pipeline,
    LrcVocalsExtractResult *result
) {
    LrcVocalsExtractRequest request;

    if (!lrc_pipeline_vocals_request(pipeline, &request)) {
        if (result) {
            lrc_vocals_extract_result_init(result);
            result->error = LRC_VOCALS_EXTRACT_ERROR_INVALID_ARGUMENT;
            if (pipeline) {
                result->message = pipeline->message;
                result->path = pipeline->path;
            } else {
                result->message = "pipeline is missing";
                result->path = NULL;
            }
        }
        return false;
    }

    if (!lrc_extract_vocals(&request, result)) {
        lrc_pipeline_error_set(
            pipeline,
            LRC_PIPELINE_ERROR_VOCALS_EXTRACT_FAILED,
            "vocals extraction failed",
            request.output_path
        );
        return false;
    }

    return true;
}

#if TESTING_pipeline

#define CBASE_IMPLEMENT
#include "cbase.h"

static void
audio_io_format_init(AudioIoFormat *format) {
    format->sample_rate = 44100;
    format->channel_count = 2;

    return;
}

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

static int32
pipeline_test_fail(char *name) {
    error2("pipeline test failed: %s\n", name);

    return 1;
}

static bool
pipeline_test_write_file(char *path) {
    FILE *file;

    if ((file = fopen(path, "wb")) == NULL) {
        return false;
    }
    if (fwrite("x", 1, 1, file) != 1) {
        fclose(file);
        return false;
    }
    if (fclose(file) != 0) {
        return false;
    }

    return true;
}

static int32
pipeline_test_config_defaults(void) {
    LrcPipelineConfig config;
    LrcPipeline pipeline;

    lrc_pipeline_config_init(&config);
    lrc_pipeline_init(&pipeline, &config);

    ASSERT(config.song_path == NULL);
    ASSERT(config.lyrics_text_path == NULL);
    ASSERT(config.existing_vocals_path == NULL);
    ASSERT(config.vocals_path == NULL);
    ASSERT(config.output_lrc_path == NULL);
    ASSERT(config.vocals_model_path == NULL);
    ASSERT(config.ctc_model_path == NULL);
    ASSERT(config.tokenizer_path == NULL);
    ASSERT(strequal(config.temp_dir, "/tmp"));
    ASSERT(strequal(config.ffmpeg_path, "ffmpeg"));
    ASSERT(strequal(config.vocals_container_format, "wav"));
    ASSERT(config.print_info);
    ASSERT(!config.keep_temp_files);
    ASSERT(config.vocals_output_format.sample_rate == 44100);
    ASSERT(config.vocals_output_format.channel_count == 2);
    ASSERT(config.mdx_config.sample_rate == 44100);
    ASSERT(config.mdx_config.channel_count == 2);
    ASSERT(pipeline.error == LRC_PIPELINE_ERROR_NONE);
    ASSERT(strequal(pipeline.message, "ok"));
    ASSERT(pipeline.path == NULL);
    ASSERT(!pipeline.prepared);
    ASSERT(!pipeline.owns_temp_dir);
    ASSERT(!pipeline.owns_vocals_path);

    return 0;
}

static int32
pipeline_test_explicit_vocals_path(void) {
    LrcPipelineConfig config;
    LrcPipeline pipeline;

    lrc_pipeline_config_init(&config);
    config.song_path = "song.flac";
    config.vocals_path = "vocals.wav";
    config.vocals_model_path = "model.onnx";

    lrc_pipeline_init(&pipeline, &config);
    if (!lrc_pipeline_prepare(&pipeline)) {
        return pipeline_test_fail("prepare explicit vocals path");
    }
    if (!strequal(pipeline.vocals_stage_path, "vocals.wav")) {
        return pipeline_test_fail("explicit vocals stage path");
    }
    if (pipeline.owns_temp_dir) {
        return pipeline_test_fail("explicit path owns temp dir");
    }
    if (pipeline.owns_vocals_path) {
        return pipeline_test_fail("explicit path owns vocals path");
    }

    lrc_pipeline_cleanup(&pipeline);

    return 0;
}

static int32
pipeline_test_owned_temp_cleanup(void) {
    LrcPipelineConfig config;
    LrcPipeline pipeline;
    char temp_root[PATH_MAX];
    char owned_dir[PATH_MAX];
    int32 len;

    test_make_temp_dir(temp_root, SIZEOF(temp_root), "pipeline_root");

    lrc_pipeline_config_init(&config);
    config.temp_dir = temp_root;
    lrc_pipeline_init(&pipeline, &config);

    if (!lrc_pipeline_prepare(&pipeline)) {
        test_remove_tree(temp_root);
        return pipeline_test_fail("prepare owned temp path");
    }
    if (!pipeline.owns_temp_dir) {
        test_remove_tree(temp_root);
        return pipeline_test_fail("owns temp dir");
    }
    if (!pipeline.owns_vocals_path) {
        test_remove_tree(temp_root);
        return pipeline_test_fail("owns vocals path");
    }
    if (!util_file_exists(pipeline.owned_temp_dir)) {
        test_remove_tree(temp_root);
        return pipeline_test_fail("owned temp dir exists");
    }
    if (!BEGINS_WITH(pipeline.owned_vocals_path,
                     strlen32(pipeline.owned_vocals_path),
                     pipeline.owned_temp_dir,
                     strlen32(pipeline.owned_temp_dir))) {
        test_remove_tree(temp_root);
        return pipeline_test_fail("owned vocals path below temp dir");
    }
    if (!pipeline_test_write_file(pipeline.owned_vocals_path)) {
        test_remove_tree(temp_root);
        return pipeline_test_fail("write owned vocals file");
    }

    len = snprintf2(owned_dir, SIZEOF(owned_dir),
                    "%s", pipeline.owned_temp_dir);
    if ((len <= 0) || (len >= SIZEOF(owned_dir))) {
        test_remove_tree(temp_root);
        return pipeline_test_fail("store owned dir");
    }

    lrc_pipeline_cleanup(&pipeline);
    if (util_file_exists(owned_dir)) {
        test_remove_tree(temp_root);
        return pipeline_test_fail("cleanup owned temp dir");
    }

    test_remove_tree(temp_root);

    return 0;
}

static int32
pipeline_test_keep_temp_files(void) {
    LrcPipelineConfig config;
    LrcPipeline pipeline;
    char temp_root[PATH_MAX];
    char owned_dir[PATH_MAX];
    int32 len;

    test_make_temp_dir(temp_root, SIZEOF(temp_root), "pipeline_keep");

    lrc_pipeline_config_init(&config);
    config.temp_dir = temp_root;
    config.keep_temp_files = true;
    lrc_pipeline_init(&pipeline, &config);

    if (!lrc_pipeline_prepare(&pipeline)) {
        test_remove_tree(temp_root);
        return pipeline_test_fail("prepare keep temp");
    }
    len = snprintf2(owned_dir, SIZEOF(owned_dir),
                    "%s", pipeline.owned_temp_dir);
    if ((len <= 0) || (len >= SIZEOF(owned_dir))) {
        test_remove_tree(temp_root);
        return pipeline_test_fail("store keep temp dir");
    }

    lrc_pipeline_cleanup(&pipeline);
    if (!util_file_exists(owned_dir)) {
        test_remove_tree(temp_root);
        return pipeline_test_fail("kept temp dir exists");
    }

    test_remove_tree(temp_root);

    return 0;
}

static int32
pipeline_test_vocals_request(void) {
    LrcPipelineConfig config;
    LrcPipeline pipeline;
    LrcVocalsExtractRequest request;

    lrc_pipeline_config_init(&config);
    config.song_path = "song.flac";
    config.vocals_path = "vocals.wav";
    config.vocals_model_path = "mdx.onnx";
    config.temp_dir = "/tmp/project-temp";
    config.ffmpeg_path = "ffmpeg-custom";
    config.vocals_container_format = "flac";
    config.print_info = false;
    config.mdx_config.chunk_seconds = 3;
    config.mdx_config.margin_seconds = 1;

    lrc_pipeline_init(&pipeline, &config);
    if (!lrc_pipeline_vocals_request(&pipeline, &request)) {
        return pipeline_test_fail("vocals request");
    }
    ASSERT(strequal(request.input_path, "song.flac"));
    ASSERT(strequal(request.output_path, "vocals.wav"));
    ASSERT(strequal(request.model_path, "mdx.onnx"));
    ASSERT(strequal(request.temp_dir, "/tmp/project-temp"));
    ASSERT(strequal(request.ffmpeg_path, "ffmpeg-custom"));
    ASSERT(strequal(request.container_format, "flac"));
    ASSERT(!request.print_info);
    ASSERT(request.mdx_config.chunk_seconds == 3);
    ASSERT(request.mdx_config.margin_seconds == 1);

    lrc_pipeline_cleanup(&pipeline);

    return 0;
}

static int32
pipeline_test_existing_vocals_path(void) {
    LrcPipelineConfig config;
    LrcPipeline pipeline;
    LrcVocalsExtractRequest request;

    lrc_pipeline_config_init(&config);
    config.existing_vocals_path = "maxwell_vocals.opus";
    lrc_pipeline_init(&pipeline, &config);

    if (!lrc_pipeline_prepare(&pipeline)) {
        return pipeline_test_fail("prepare existing vocals path");
    }
    ASSERT(strequal(pipeline.vocals_stage_path, "maxwell_vocals.opus"));
    ASSERT(!pipeline.owns_temp_dir);
    ASSERT(!pipeline.owns_vocals_path);
    if (lrc_pipeline_vocals_request(&pipeline, &request)) {
        return pipeline_test_fail("existing vocals extraction accepted");
    }
    ASSERT(pipeline.error == LRC_PIPELINE_ERROR_VOCALS_ALREADY_AVAILABLE);

    lrc_pipeline_cleanup(&pipeline);

    return 0;
}

static int32
pipeline_test_optional_maxwell_config(void) {
    LrcPipelineConfig config;
    LrcPipeline pipeline;
    char *song_path;
    char *lyrics_path;
    char *vocals_path;
    char *lrc_path;

    song_path = getenv("LRC_TEST_MAXWELL_FLAC");
    lyrics_path = getenv("LRC_TEST_MAXWELL_TXT");
    vocals_path = getenv("LRC_TEST_MAXWELL_VOCALS");
    lrc_path = getenv("LRC_TEST_MAXWELL_LRC");

    if (song_path == NULL) {
        song_path = "../maxwell.flac";
    }
    if (lyrics_path == NULL) {
        lyrics_path = "../maxwell.txt";
    }
    if (vocals_path == NULL) {
        vocals_path = "../maxwell_vocals.opus";
    }
    if (lrc_path == NULL) {
        lrc_path = "../maxwell.lrc";
    }

    if (!util_file_exists(song_path)
        || !util_file_exists(lyrics_path)
        || !util_file_exists(vocals_path)
        || !util_file_exists(lrc_path)) {
        return 0;
    }

    lrc_pipeline_config_init(&config);
    config.song_path = song_path;
    config.lyrics_text_path = lyrics_path;
    config.existing_vocals_path = vocals_path;
    config.output_lrc_path = lrc_path;
    lrc_pipeline_init(&pipeline, &config);

    if (!lrc_pipeline_prepare(&pipeline)) {
        return pipeline_test_fail("prepare maxwell fixture config");
    }
    ASSERT(strequal(pipeline.config.song_path, song_path));
    ASSERT(strequal(pipeline.config.lyrics_text_path, lyrics_path));
    ASSERT(strequal(pipeline.vocals_stage_path, vocals_path));
    ASSERT(strequal(pipeline.config.output_lrc_path, lrc_path));
    ASSERT(!pipeline.owns_temp_dir);
    ASSERT(!pipeline.owns_vocals_path);

    lrc_pipeline_cleanup(&pipeline);

    return 0;
}

int32
main(void) {
    if (pipeline_test_config_defaults() != 0) {
        exit(1);
    }
    if (pipeline_test_explicit_vocals_path() != 0) {
        exit(1);
    }
    if (pipeline_test_owned_temp_cleanup() != 0) {
        exit(1);
    }
    if (pipeline_test_keep_temp_files() != 0) {
        exit(1);
    }
    if (pipeline_test_vocals_request() != 0) {
        exit(1);
    }
    if (pipeline_test_existing_vocals_path() != 0) {
        exit(1);
    }
    if (pipeline_test_optional_maxwell_config() != 0) {
        exit(1);
    }

    return 0;
}

#endif /* TESTING_pipeline */
