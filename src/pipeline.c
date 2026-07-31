#include "cbase.h"

#if !defined(TESTING_pipeline)
#define TESTING_pipeline 0
#endif

#if !defined(LRC_PIPELINE_ENABLE_GENERATE)
#define LRC_PIPELINE_ENABLE_GENERATE TESTING_pipeline
#endif

#include "pipeline.h"

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

static void
lrc_pipeline_vocals_result_set(
    LrcVocalsExtractResult *result,
    enum LrcVocalsExtractError error,
    char *message,
    char *path
) {
    if (result == NULL) {
        return;
    }

    result->error = error;
    result->message = message;
    result->path = path;

    return;
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
    lrc_ctc_model_config_init(&config->ctc_model_config);
    config->ctc_emission_values_kind = LRC_CTC_EMISSION_VALUES_LOGITS;

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

    lrc_ctc_assets_init(&pipeline->ctc_assets);

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

static void
lrc_pipeline_ctc_assets_config(
    LrcPipeline *pipeline,
    LrcCtcAssetsConfig *config
) {
    if (config == NULL) {
        return;
    }

    lrc_ctc_assets_config_init(config);
    if (pipeline == NULL) {
        return;
    }

    config->model_path = pipeline->config.ctc_model_path;
    config->tokenizer_path = pipeline->config.tokenizer_path;

    return;
}

static bool
lrc_pipeline_validate_ctc_assets(
    LrcPipeline *pipeline,
    LrcCtcAssetsResult *result
) {
    LrcCtcAssetsConfig config;

    if (pipeline == NULL) {
        lrc_ctc_assets_result_init(result);
        if (result) {
            result->error = LRC_CTC_ASSETS_ERROR_INVALID_ARGUMENT;
            result->message = "pipeline is missing";
            result->path = NULL;
        }
        return false;
    }

    lrc_pipeline_ctc_assets_config(pipeline, &config);
    if (!lrc_ctc_assets_validate(&pipeline->ctc_assets, &config, result)) {
        lrc_pipeline_error_set(
            pipeline,
            LRC_PIPELINE_ERROR_CTC_ASSETS_INVALID,
            "CTC assets are invalid",
            NULL
        );
        if (result && result->path) {
            pipeline->path = result->path;
        }
        return false;
    }

    return true;
}

static bool
lrc_pipeline_extract_vocals(
    LrcPipeline *pipeline,
    LrcVocalsExtractResult *result
) {
    LrcVocalsExtractRequest request;

    lrc_pipeline_vocals_result_set(
        result,
        LRC_VOCALS_EXTRACT_ERROR_INVALID_ARGUMENT,
        "pipeline is missing",
        NULL
    );

    if (!lrc_pipeline_vocals_request(pipeline, &request)) {
        if (pipeline != NULL) {
            lrc_pipeline_vocals_result_set(
                result,
                LRC_VOCALS_EXTRACT_ERROR_INVALID_ARGUMENT,
                pipeline->message,
                pipeline->path
            );
        }
        return false;
    }

    lrc_pipeline_vocals_result_set(
        result,
        LRC_VOCALS_EXTRACT_ERROR_MDX_PROCESS_FAILED,
        "vocals extraction failed",
        request.output_path
    );

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

#if LRC_PIPELINE_ENABLE_GENERATE
static void
lrc_pipeline_generate_result_init(LrcPipelineGenerateResult *result) {
    if (result == NULL) {
        return;
    }

    result->error = LRC_PIPELINE_GENERATE_ERROR_NONE;
    result->message = "ok";
    result->path = NULL;

    result->frame_index = -1;
    result->token_index = -1;
    result->line_index = -1;

    return;
}

static void
lrc_pipeline_generate_result_set(
    LrcPipelineGenerateResult *result,
    enum LrcPipelineGenerateError error,
    char *message,
    char *path
) {
    if (result == NULL) {
        return;
    }

    result->error = error;
    result->message = message;
    result->path = path;

    return;
}

static bool
lrc_pipeline_output_lines_from_timestamps(
    LrcLyrics *lyrics,
    LrcCtcLineTimestamps *timestamps,
    LrcOutputLine *lines,
    LrcPipelineGenerateResult *result
) {
    if ((lyrics == NULL) || (timestamps == NULL) || (lines == NULL)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_INVALID_ARGUMENT,
            "LRC output line conversion arguments are invalid",
            NULL
        );
        return false;
    }
    if ((timestamps->line_count < 0)
        || (timestamps->line_count > INT32_MAX)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_TOO_LARGE,
            "too many LRC output lines",
            NULL
        );
        return false;
    }

    for (int64 i = 0; i < timestamps->line_count; i += 1) {
        LrcCtcLineTimestamp *timestamp;
        LrcLyricsLine *lyrics_line;
        LrcFormatResult format_result;
        int32 hundredths;

        timestamp = timestamps->lines + i;
        if ((timestamp->line_index < 0)
            || (timestamp->line_index >= lyrics->line_count)) {
            lrc_pipeline_generate_result_set(
                result,
                LRC_PIPELINE_GENERATE_ERROR_OUTPUT_LINES_FAILED,
                "LRC timestamp line index is invalid",
                NULL
            );
            if (result) {
                result->line_index = timestamp->line_index;
            }
            return false;
        }

        lyrics_line = lyrics->lines + timestamp->line_index;
        lines[i].text = lyrics_line->text;
        lines[i].text_len = lyrics_line->text_len;
        lines[i].timestamp_hundredths = -1;

        switch (timestamp->kind) {
        case LRC_CTC_LINE_TIMESTAMP_KIND_TIMESTAMPED:
            if (!lrc_timestamp_hundredths_from_seconds(
                timestamp->start_seconds,
                &hundredths,
                &format_result
            )) {
                lrc_pipeline_generate_result_set(
                    result,
                    LRC_PIPELINE_GENERATE_ERROR_OUTPUT_LINES_FAILED,
                    "could not format LRC timestamp",
                    NULL
                );
                return false;
            }
            lines[i].kind = LRC_OUTPUT_LINE_KIND_TIMESTAMPED;
            lines[i].timestamp_hundredths = hundredths;
            break;
        case LRC_CTC_LINE_TIMESTAMP_KIND_BLANK:
            lines[i].kind = LRC_OUTPUT_LINE_KIND_BLANK;
            break;
        default:
            lrc_pipeline_generate_result_set(
                result,
                LRC_PIPELINE_GENERATE_ERROR_OUTPUT_LINES_FAILED,
                "LRC timestamp kind is invalid",
                NULL
            );
            return false;
        }
    }

    return true;
}

static bool
lrc_pipeline_generate_targets(
    LrcCtcTokenizedText *tokens,
    int32 **target_token_ids,
    int64 *target_token_count,
    LrcPipelineGenerateResult *result
) {
    int64 count;

    if ((tokens == NULL) || (target_token_ids == NULL)
        || (target_token_count == NULL) || (tokens->tokens == NULL)
        || (tokens->token_count <= 0)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_TOKENIZE_FAILED,
            "CTC tokenized lyrics are empty",
            NULL
        );
        return false;
    }

    count = tokens->token_count;
    if (count > INT64_MAX/SIZEOF(**target_token_ids)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_TOO_LARGE,
            "CTC target token allocation is too large",
            NULL
        );
        return false;
    }

    *target_token_ids = malloc2(count*SIZEOF(**target_token_ids));
    *target_token_count = count;
    for (int64 i = 0; i < count; i += 1) {
        (*target_token_ids)[i] = tokens->tokens[i].token_id;
    }

    return true;
}

static bool
lrc_pipeline_prepare_vocals_stage_for_generation(
    LrcPipeline *pipeline,
    LrcPipelineGenerateResult *result
) {
    LrcVocalsExtractResult vocals_result;

    if (!lrc_pipeline_prepare(pipeline)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_PREPARE_FAILED,
            pipeline->message,
            pipeline->path
        );
        return false;
    }

    if (!lrc_pipeline_path_missing(pipeline->config.existing_vocals_path)) {
        return true;
    }

    if (!lrc_pipeline_extract_vocals(pipeline, &vocals_result)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_VOCALS_EXTRACT_FAILED,
            vocals_result.message,
            vocals_result.path
        );
        return false;
    }

    return true;
}

static bool
lrc_generate_config_path_ready(
    char *path,
    enum LrcPipelineGenerateError error,
    char *message,
    LrcPipelineGenerateResult *result
) {
    if (!lrc_pipeline_path_missing(path)) {
        return true;
    }

    lrc_pipeline_generate_result_set(result, error, message, path);

    return false;
}

static bool
lrc_generate_from_song(
    LrcPipelineConfig *config,
    LrcPipelineGenerateResult *result
) {
    LrcPipeline pipeline;
    bool ok;

    if (result) {
        lrc_pipeline_generate_result_init(result);
    }
    if (config == NULL) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_INVALID_ARGUMENT,
            "generation configuration is missing",
            NULL
        );
        return false;
    }
    if (!lrc_generate_config_path_ready(
        config->song_path,
        LRC_PIPELINE_GENERATE_ERROR_MISSING_SONG,
        "input song path is missing",
        result
    )) {
        return false;
    }
    if (!lrc_generate_config_path_ready(
        config->lyrics_text_path,
        LRC_PIPELINE_GENERATE_ERROR_MISSING_LYRICS,
        "lyrics text path is missing",
        result
    )) {
        return false;
    }
    if (!lrc_generate_config_path_ready(
        config->output_lrc_path,
        LRC_PIPELINE_GENERATE_ERROR_MISSING_OUTPUT,
        "output LRC path is missing",
        result
    )) {
        return false;
    }
    if (!lrc_generate_config_path_ready(
        config->vocals_model_path,
        LRC_PIPELINE_GENERATE_ERROR_MISSING_VOCALS_MODEL,
        "vocals model path is missing",
        result
    )) {
        return false;
    }
    if (!lrc_generate_config_path_ready(
        config->ctc_model_path,
        LRC_PIPELINE_GENERATE_ERROR_MISSING_CTC_MODEL,
        "CTC model path is missing",
        result
    )) {
        return false;
    }
    if (!lrc_generate_config_path_ready(
        config->tokenizer_path,
        LRC_PIPELINE_GENERATE_ERROR_MISSING_TOKENIZER,
        "CTC tokenizer path is missing",
        result
    )) {
        return false;
    }

    lrc_pipeline_init(&pipeline, config);
    ok = lrc_pipeline_generate_lrc(&pipeline, result);
    lrc_pipeline_cleanup(&pipeline);

    return ok;
}

static bool
lrc_pipeline_generate_lrc(
    LrcPipeline *pipeline,
    LrcPipelineGenerateResult *result
) {
    LrcCtcAssetsResult assets_result;
    LrcLyrics lyrics;
    LrcLyricsLoadResult lyrics_result;
    LrcLyricsNormalized normalized;
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizerResult tokenizer_result;
    LrcCtcTokenizedText tokens;
    LrcCtcTokenizeResult tokenize_result;
    LrcCtcAudioConfig audio_config;
    LrcCtcAudioResult audio_result;
    LrcCtcAudio audio;
    LrcCtcModelInputResult model_result;
    LrcCtcModelInput input;
    LrcCtcOnnxInference onnx;
    LrcCtcInferenceBackend backend;
    LrcCtcInferenceResult inference_result;
    LrcCtcEmissions emissions;
    LrcCtcAlignResult align_result;
    LrcCtcTrellis trellis;
    LrcCtcPath path;
    LrcCtcTokenSpans token_spans;
    LrcCtcWordSpans word_spans;
    LrcCtcLineTimestamps line_timestamps;
    LrcOutputLine *output_lines;
    LrcWriteResult write_result;
    int32 *target_token_ids;
    int64 target_token_count;
    int32 star_token_id;
    float frame_duration_seconds;
    bool ok;

    if (result) {
        lrc_pipeline_generate_result_init(result);
    }
    if (pipeline == NULL) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_INVALID_ARGUMENT,
            "pipeline is missing",
            NULL
        );
        return false;
    }
    if (lrc_pipeline_path_missing(pipeline->config.lyrics_text_path)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_MISSING_LYRICS,
            "lyrics text path is missing",
            pipeline->config.lyrics_text_path
        );
        return false;
    }
    if (lrc_pipeline_path_missing(pipeline->config.output_lrc_path)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_MISSING_OUTPUT,
            "output LRC path is missing",
            pipeline->config.output_lrc_path
        );
        return false;
    }

    lrc_lyrics_init(&lyrics);
    lrc_lyrics_normalized_init(&normalized);
    lrc_ctc_tokenizer_init(&tokenizer);
    lrc_ctc_tokenized_text_init(&tokens);
    lrc_ctc_audio_init(&audio);
    lrc_ctc_model_input_init(&input);
    lrc_ctc_onnx_inference_init(&onnx);
    lrc_ctc_emissions_init(&emissions);
    lrc_ctc_trellis_init(&trellis);
    lrc_ctc_path_init(&path);
    lrc_ctc_token_spans_init(&token_spans);
    lrc_ctc_word_spans_init(&word_spans);
    lrc_ctc_line_timestamps_init(&line_timestamps);

    output_lines = NULL;
    target_token_ids = NULL;
    target_token_count = 0;
    star_token_id = -1;
    ok = true;

    if (ok && !lrc_pipeline_prepare_vocals_stage_for_generation(pipeline,
                                                                 result)) {
        ok = false;
    }
    if (ok && !lrc_pipeline_validate_ctc_assets(pipeline, &assets_result)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_CTC_ASSETS_INVALID,
            assets_result.message,
            assets_result.path
        );
        ok = false;
    }
    if (ok && !lrc_lyrics_load_file(&lyrics,
                                    pipeline->config.lyrics_text_path,
                                    &lyrics_result)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_LYRICS_LOAD_FAILED,
            lyrics_result.message,
            lyrics_result.path
        );
        ok = false;
    }
    if (ok && !lrc_lyrics_normalize(&lyrics, &normalized)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_LYRICS_NORMALIZE_FAILED,
            "could not normalize lyrics",
            pipeline->config.lyrics_text_path
        );
        ok = false;
    }
    if (ok && !lrc_ctc_tokenizer_load_file(&tokenizer,
                                           pipeline->ctc_assets.tokenizer_path,
                                           &tokenizer_result)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_TOKENIZER_LOAD_FAILED,
            tokenizer_result.message,
            tokenizer_result.path
        );
        ok = false;
    }
    if (ok && !lrc_ctc_tokenizer_tokenize_normalized(&tokenizer,
                                                     &normalized,
                                                     &tokens,
                                                     &tokenize_result)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_TOKENIZE_FAILED,
            tokenize_result.message,
            NULL
        );
        if (result) {
            result->line_index = tokenize_result.line_index;
            result->token_index = tokenize_result.token_id;
        }
        ok = false;
    }

    lrc_ctc_audio_config_init(&audio_config);
    audio_config.ffmpeg_path = pipeline->config.ffmpeg_path;
    audio_config.sample_rate = pipeline->config.ctc_model_config.sample_rate;
    if (ok && !lrc_ctc_audio_decode_file(&audio,
                                         pipeline->vocals_stage_path,
                                         &audio_config,
                                         &audio_result)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_AUDIO_DECODE_FAILED,
            audio_result.message,
            audio_result.path
        );
        if (result) {
            result->frame_index = audio_result.sample_index;
        }
        ok = false;
    }
    if (ok && !lrc_ctc_model_input_prepare(&input,
                                           &audio,
                                           &pipeline->config.ctc_model_config,
                                           &model_result)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_MODEL_INPUT_FAILED,
            model_result.message,
            pipeline->vocals_stage_path
        );
        if (result) {
            result->frame_index = model_result.sample_index;
        }
        ok = false;
    }
    if (ok && !lrc_ctc_onnx_inference_load(&onnx,
                                           pipeline->ctc_assets.model_path,
                                           &inference_result)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_CTC_MODEL_LOAD_FAILED,
            inference_result.message,
            pipeline->ctc_assets.model_path
        );
        ok = false;
    }
    if (ok) {
        lrc_ctc_onnx_inference_backend(&onnx, &backend);
        backend.values_kind = pipeline->config.ctc_emission_values_kind;
        backend.print_progress = pipeline->config.print_info;
        if (!lrc_ctc_inference_run(&backend,
                                   &input,
                                   &emissions,
                                   &inference_result)) {
            lrc_pipeline_generate_result_set(
                result,
                LRC_PIPELINE_GENERATE_ERROR_CTC_INFERENCE_FAILED,
                inference_result.message,
                pipeline->ctc_assets.model_path
            );
            if (result) {
                result->frame_index = inference_result.output_index;
            }
            ok = false;
        }
    }
    if (ok && !lrc_pipeline_generate_targets(&tokens,
                                             &target_token_ids,
                                             &target_token_count,
                                             result)) {
        ok = false;
    }
    if (ok) {
        if (emissions.vocabulary_size > MAXOF(star_token_id)) {
            lrc_pipeline_generate_result_set(
                result,
                LRC_PIPELINE_GENERATE_ERROR_TOO_LARGE,
                "CTC vocabulary is too large for a star token",
                NULL
            );
            ok = false;
        } else {
            star_token_id = (int32)emissions.vocabulary_size;
        }
    }
    if (ok && !lrc_ctc_trellis_score_forward_with_edge_stars(
        &trellis,
        &emissions,
        target_token_ids,
        target_token_count,
        tokenizer.blank_id,
        star_token_id,
        &align_result
    )) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_ALIGNMENT_FAILED,
            align_result.message,
            NULL
        );
        if (result) {
            result->frame_index = align_result.frame_index;
            result->token_index = align_result.token_index;
        }
        ok = false;
    }
    if (ok && !lrc_ctc_trellis_backtrack_with_edge_stars(&trellis,
                                                          &emissions,
                                                          target_token_ids,
                                                          target_token_count,
                                                          tokenizer.blank_id,
                                                          star_token_id,
                                                          &path,
                                                          &align_result)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_ALIGNMENT_FAILED,
            align_result.message,
            NULL
        );
        if (result) {
            result->frame_index = align_result.frame_index;
            result->token_index = align_result.token_index;
        }
        ok = false;
    }

    frame_duration_seconds = (float)(input.stride_ms/1000.0);
    if (ok && !lrc_ctc_path_to_token_spans(&path,
                                           &emissions,
                                           frame_duration_seconds,
                                           &token_spans,
                                           &align_result)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_ALIGNMENT_FAILED,
            align_result.message,
            NULL
        );
        ok = false;
    }
    if (ok && !lrc_ctc_token_spans_to_word_spans(&token_spans,
                                                 &tokens,
                                                 &normalized,
                                                 &word_spans,
                                                 &align_result)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_ALIGNMENT_FAILED,
            align_result.message,
            NULL
        );
        ok = false;
    }
    if (ok && !lrc_ctc_word_spans_to_line_timestamps(&word_spans,
                                                     &normalized,
                                                     &line_timestamps,
                                                     &align_result)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_ALIGNMENT_FAILED,
            align_result.message,
            NULL
        );
        ok = false;
    }
    if (ok && (line_timestamps.line_count > INT64_MAX/SIZEOF(*output_lines))) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_TOO_LARGE,
            "LRC output line allocation is too large",
            NULL
        );
        ok = false;
    }
    if (ok) {
        output_lines = malloc2(
            line_timestamps.line_count*SIZEOF(*output_lines)
        );
        if (!lrc_pipeline_output_lines_from_timestamps(&lyrics,
                                                       &line_timestamps,
                                                       output_lines,
                                                       result)) {
            ok = false;
        }
    }
    if (ok && !lrc_write_output_file(pipeline->config.output_lrc_path,
                                     output_lines,
                                     (int32)line_timestamps.line_count,
                                     &write_result)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_LRC_WRITE_FAILED,
            write_result.message,
            write_result.path
        );
        if (result) {
            result->line_index = write_result.line_index;
        }
        ok = false;
    }

    if (!ok) {
        char *message;
        char *path_arg;

        message = "LRC generation failed";
        path_arg = NULL;
        if (result) {
            message = result->message;
            path_arg = result->path;
        }
        lrc_pipeline_error_set(pipeline,
                               LRC_PIPELINE_ERROR_GENERATE_FAILED,
                               message,
                               path_arg);
    }

    if (output_lines) {
        free2(output_lines,
              line_timestamps.line_count*SIZEOF(*output_lines));
    }
    if (target_token_ids) {
        free2(target_token_ids,
              target_token_count*SIZEOF(*target_token_ids));
    }
    lrc_ctc_line_timestamps_destroy(&line_timestamps);
    lrc_ctc_word_spans_destroy(&word_spans);
    lrc_ctc_token_spans_destroy(&token_spans);
    lrc_ctc_path_destroy(&path);
    lrc_ctc_trellis_destroy(&trellis);
    lrc_ctc_emissions_destroy(&emissions);
    lrc_ctc_onnx_inference_destroy(&onnx);
    lrc_ctc_model_input_destroy(&input);
    lrc_ctc_audio_destroy(&audio);
    lrc_ctc_tokenized_text_destroy(&tokens);
    lrc_ctc_tokenizer_destroy(&tokenizer);
    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);

    return ok;
}
#endif

#if TESTING_pipeline

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
lrc_ctc_model_config_init(LrcCtcModelConfig *config) {
    memset64(config, 0, SIZEOF(*config));

    config->sample_rate = LRC_CTC_AUDIO_DEFAULT_SAMPLE_RATE;
    config->inputs_to_logits_ratio =
        LRC_CTC_MODEL_DEFAULT_INPUTS_TO_LOGITS_RATIO;
    config->window_seconds = LRC_CTC_MODEL_DEFAULT_WINDOW_SECONDS;
    config->context_seconds = LRC_CTC_MODEL_DEFAULT_CONTEXT_SECONDS;

    return;
}

static void
lrc_pipeline_test_noop(void *pointer) {
    (void)pointer;

    return;
}

static void
lrc_lyrics_init(LrcLyrics *lyrics) {
    memset64(lyrics, 0, SIZEOF(*lyrics));

    return;
}

static void
lrc_lyrics_destroy(LrcLyrics *lyrics) {
    lrc_pipeline_test_noop(lyrics);

    return;
}

static bool
lrc_lyrics_load_file(
    LrcLyrics *lyrics,
    char *path,
    LrcLyricsLoadResult *result
) {
    (void)lyrics;
    (void)path;
    (void)result;

    return false;
}

static void
lrc_lyrics_normalized_init(LrcLyricsNormalized *normalized) {
    memset64(normalized, 0, SIZEOF(*normalized));

    return;
}

static void
lrc_lyrics_normalized_destroy(LrcLyricsNormalized *normalized) {
    lrc_pipeline_test_noop(normalized);

    return;
}

static bool
lrc_lyrics_normalize(
    LrcLyrics *lyrics,
    LrcLyricsNormalized *normalized
) {
    (void)lyrics;
    (void)normalized;

    return false;
}

static void
lrc_ctc_tokenize_result_init(LrcCtcTokenizeResult *result) {
    memset64(result, 0, SIZEOF(*result));

    result->error = LRC_CTC_TOKENIZE_ERROR_NONE;
    result->message = "ok";
    result->byte_offset = -1;
    result->line_index = -1;
    result->token_id = -1;

    return;
}

static void
lrc_ctc_tokenized_text_init(LrcCtcTokenizedText *text) {
    memset64(text, 0, SIZEOF(*text));

    return;
}

static void
lrc_ctc_tokenized_text_destroy(LrcCtcTokenizedText *text) {
    lrc_pipeline_test_noop(text);

    return;
}

static void
lrc_ctc_tokenizer_init(LrcCtcTokenizer *tokenizer) {
    memset64(tokenizer, 0, SIZEOF(*tokenizer));

    return;
}

static void
lrc_ctc_tokenizer_destroy(LrcCtcTokenizer *tokenizer) {
    lrc_pipeline_test_noop(tokenizer);

    return;
}

static void
lrc_ctc_tokenizer_result_init(LrcCtcTokenizerResult *result) {
    memset64(result, 0, SIZEOF(*result));

    result->error = LRC_CTC_TOKENIZER_ERROR_NONE;
    result->message = "ok";
    result->line_index = -1;
    result->token_id = -1;

    return;
}

static bool
lrc_ctc_tokenizer_load_file(
    LrcCtcTokenizer *tokenizer,
    char *path,
    LrcCtcTokenizerResult *result
) {
    (void)tokenizer;
    (void)path;
    lrc_ctc_tokenizer_result_init(result);

    return false;
}

static bool
lrc_ctc_tokenizer_tokenize_normalized(
    LrcCtcTokenizer *tokenizer,
    LrcLyricsNormalized *normalized,
    LrcCtcTokenizedText *tokens,
    LrcCtcTokenizeResult *result
) {
    (void)tokenizer;
    (void)normalized;
    (void)tokens;
    lrc_ctc_tokenize_result_init(result);

    return false;
}

static void
lrc_ctc_audio_config_init(LrcCtcAudioConfig *config) {
    memset64(config, 0, SIZEOF(*config));

    config->ffmpeg_path = "ffmpeg";
    config->sample_rate = LRC_CTC_AUDIO_DEFAULT_SAMPLE_RATE;

    return;
}

static void
lrc_ctc_audio_init(LrcCtcAudio *audio) {
    memset64(audio, 0, SIZEOF(*audio));

    return;
}

static void
lrc_ctc_audio_destroy(LrcCtcAudio *audio) {
    lrc_pipeline_test_noop(audio);

    return;
}

static bool
lrc_ctc_audio_decode_file(
    LrcCtcAudio *audio,
    char *path,
    LrcCtcAudioConfig *config,
    LrcCtcAudioResult *result
) {
    (void)audio;
    (void)path;
    (void)config;
    (void)result;

    return false;
}

static void
lrc_ctc_model_input_init(LrcCtcModelInput *input) {
    memset64(input, 0, SIZEOF(*input));

    return;
}

static void
lrc_ctc_model_input_destroy(LrcCtcModelInput *input) {
    lrc_pipeline_test_noop(input);

    return;
}

static bool
lrc_ctc_model_input_prepare(
    LrcCtcModelInput *input,
    LrcCtcAudio *audio,
    LrcCtcModelConfig *config,
    LrcCtcModelInputResult *result
) {
    (void)input;
    (void)audio;
    (void)config;
    (void)result;

    return false;
}

static void
lrc_ctc_onnx_inference_init(LrcCtcOnnxInference *onnx) {
    memset64(onnx, 0, SIZEOF(*onnx));

    return;
}

static void
lrc_ctc_onnx_inference_destroy(LrcCtcOnnxInference *onnx) {
    lrc_pipeline_test_noop(onnx);

    return;
}

static bool
lrc_ctc_onnx_inference_load(
    LrcCtcOnnxInference *onnx,
    char *model_path,
    LrcCtcInferenceResult *result
) {
    (void)onnx;
    (void)model_path;
    (void)result;

    return false;
}

static void
lrc_ctc_onnx_inference_backend(
    LrcCtcOnnxInference *onnx,
    LrcCtcInferenceBackend *backend
) {
    backend->backend = onnx;
    backend->run = NULL;

    return;
}

static bool
lrc_ctc_inference_run(
    LrcCtcInferenceBackend *backend,
    LrcCtcModelInput *input,
    LrcCtcEmissions *emissions,
    LrcCtcInferenceResult *result
) {
    (void)backend;
    (void)input;
    (void)emissions;
    (void)result;

    return false;
}

static void
lrc_ctc_emissions_init(LrcCtcEmissions *emissions) {
    memset64(emissions, 0, SIZEOF(*emissions));

    return;
}

static void
lrc_ctc_emissions_destroy(LrcCtcEmissions *emissions) {
    lrc_pipeline_test_noop(emissions);

    return;
}

static bool
lrc_ctc_emissions_convert_to_log_probabilities(
    LrcCtcEmissions *emissions,
    enum LrcCtcEmissionValuesKind values_kind,
    LrcCtcInferenceResult *result
) {
    (void)emissions;
    (void)values_kind;
    (void)result;

    return false;
}

static void
lrc_ctc_trellis_init(LrcCtcTrellis *trellis) {
    memset64(trellis, 0, SIZEOF(*trellis));

    return;
}

static void
lrc_ctc_trellis_destroy(LrcCtcTrellis *trellis) {
    lrc_pipeline_test_noop(trellis);

    return;
}

static bool
lrc_ctc_trellis_score_forward(
    LrcCtcTrellis *trellis,
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    int64 target_token_count,
    int32 blank_token_id,
    LrcCtcAlignResult *result
) {
    (void)trellis;
    (void)emissions;
    (void)target_token_ids;
    (void)target_token_count;
    (void)blank_token_id;
    (void)result;

    return false;
}

static bool
lrc_ctc_trellis_score_forward_with_edge_stars(
    LrcCtcTrellis *trellis,
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    int64 target_token_count,
    int32 blank_token_id,
    int32 star_token_id,
    LrcCtcAlignResult *result
) {
    (void)trellis;
    (void)emissions;
    (void)target_token_ids;
    (void)target_token_count;
    (void)blank_token_id;
    (void)star_token_id;
    (void)result;

    return false;
}

static bool
lrc_ctc_trellis_backtrack(
    LrcCtcTrellis *trellis,
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    int64 target_token_count,
    int32 blank_token_id,
    LrcCtcPath *path,
    LrcCtcAlignResult *result
) {
    (void)trellis;
    (void)emissions;
    (void)target_token_ids;
    (void)target_token_count;
    (void)blank_token_id;
    (void)path;
    (void)result;

    return false;
}

static bool
lrc_ctc_trellis_backtrack_with_edge_stars(
    LrcCtcTrellis *trellis,
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    int64 target_token_count,
    int32 blank_token_id,
    int32 star_token_id,
    LrcCtcPath *path,
    LrcCtcAlignResult *result
) {
    (void)trellis;
    (void)emissions;
    (void)target_token_ids;
    (void)target_token_count;
    (void)blank_token_id;
    (void)star_token_id;
    (void)path;
    (void)result;

    return false;
}

static void
lrc_ctc_path_init(LrcCtcPath *path) {
    memset64(path, 0, SIZEOF(*path));

    return;
}

static void
lrc_ctc_path_destroy(LrcCtcPath *path) {
    lrc_pipeline_test_noop(path);

    return;
}

static bool
lrc_ctc_path_to_token_spans(
    LrcCtcPath *path,
    LrcCtcEmissions *emissions,
    float frame_duration_seconds,
    LrcCtcTokenSpans *spans,
    LrcCtcAlignResult *result
) {
    (void)path;
    (void)emissions;
    (void)frame_duration_seconds;
    (void)spans;
    (void)result;

    return false;
}

static void
lrc_ctc_token_spans_init(LrcCtcTokenSpans *spans) {
    memset64(spans, 0, SIZEOF(*spans));

    return;
}

static void
lrc_ctc_token_spans_destroy(LrcCtcTokenSpans *spans) {
    lrc_pipeline_test_noop(spans);

    return;
}

static bool
lrc_ctc_token_spans_to_word_spans(
    LrcCtcTokenSpans *token_spans,
    LrcCtcTokenizedText *tokens,
    LrcLyricsNormalized *normalized,
    LrcCtcWordSpans *word_spans,
    LrcCtcAlignResult *result
) {
    (void)token_spans;
    (void)tokens;
    (void)normalized;
    (void)word_spans;
    (void)result;

    return false;
}

static void
lrc_ctc_word_spans_init(LrcCtcWordSpans *spans) {
    memset64(spans, 0, SIZEOF(*spans));

    return;
}

static void
lrc_ctc_word_spans_destroy(LrcCtcWordSpans *spans) {
    lrc_pipeline_test_noop(spans);

    return;
}

static bool
lrc_ctc_word_spans_to_line_timestamps(
    LrcCtcWordSpans *word_spans,
    LrcLyricsNormalized *normalized,
    LrcCtcLineTimestamps *line_timestamps,
    LrcCtcAlignResult *result
) {
    (void)word_spans;
    (void)normalized;
    (void)line_timestamps;
    (void)result;

    return false;
}

static void
lrc_ctc_line_timestamps_init(LrcCtcLineTimestamps *timestamps) {
    memset64(timestamps, 0, SIZEOF(*timestamps));

    return;
}

static void
lrc_ctc_line_timestamps_destroy(LrcCtcLineTimestamps *timestamps) {
    lrc_pipeline_test_noop(timestamps);

    return;
}

static bool
lrc_timestamp_hundredths_from_seconds(
    float seconds,
    int32 *timestamp_hundredths,
    LrcFormatResult *result
) {
    (void)seconds;
    (void)timestamp_hundredths;
    (void)result;

    return false;
}

static bool
lrc_write_output_file(
    char *path,
    LrcOutputLine *lines,
    int32 line_count,
    LrcWriteResult *result
) {
    (void)path;
    (void)lines;
    (void)line_count;
    (void)result;

    return false;
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


#define CBASE_IMPLEMENT
#include "cbase.h"

#include "ctc_assets.c"

static int32
pipeline_test_fail(char *name) {
    error2("pipeline test failed: %s\n", name);

    return 1;
}

static bool
pipeline_test_write_file(char *path) {
    return write_entire_file(path, STRLIT("x"));
}

static void
pipeline_test_join_path(
    char *buffer,
    int64 buffer_len,
    char *dir,
    char *name
) {
    int32 len;

    len = snprintf2(buffer, buffer_len, "%s/%s", dir, name);
    ASSERT(len > 0);
    ASSERT(len < buffer_len);

    return;
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
    ASSERT(config.ctc_model_config.sample_rate == 16000);
    ASSERT(config.ctc_model_config.inputs_to_logits_ratio == 320);
    ASSERT(config.ctc_model_config.window_seconds == 30);
    ASSERT(config.ctc_model_config.context_seconds == 2);
    ASSERT(config.ctc_emission_values_kind
           == LRC_CTC_EMISSION_VALUES_LOGITS);
    ASSERT(pipeline.error == LRC_PIPELINE_ERROR_NONE);
    ASSERT(strequal(pipeline.message, "ok"));
    ASSERT(pipeline.path == NULL);
    ASSERT(!pipeline.prepared);
    ASSERT(!pipeline.owns_temp_dir);
    ASSERT(!pipeline.owns_vocals_path);
    ASSERT(pipeline.ctc_assets.model_path == NULL);
    ASSERT(pipeline.ctc_assets.tokenizer_path == NULL);
    ASSERT(!pipeline.ctc_assets.validated);

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
pipeline_test_ctc_assets_config(void) {
    LrcPipelineConfig config;
    LrcPipeline pipeline;
    LrcCtcAssetsConfig assets_config;

    lrc_pipeline_config_init(&config);
    config.ctc_model_path = "ctc.onnx";
    config.tokenizer_path = "tokens.txt";
    lrc_pipeline_init(&pipeline, &config);

    lrc_pipeline_ctc_assets_config(&pipeline, &assets_config);
    ASSERT(strequal(assets_config.model_path, "ctc.onnx"));
    ASSERT(strequal(assets_config.tokenizer_path, "tokens.txt"));

    return 0;
}

static int32
pipeline_test_ctc_assets_validate(void) {
    LrcPipelineConfig config;
    LrcPipeline pipeline;
    LrcCtcAssetsResult result;
    char temp_dir[PATH_MAX];
    char model_path[PATH_MAX];
    char tokenizer_path[PATH_MAX];

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "pipeline_ctc_assets");
    pipeline_test_join_path(model_path, SIZEOF(model_path), temp_dir,
                            "ctc.onnx");
    pipeline_test_join_path(tokenizer_path, SIZEOF(tokenizer_path), temp_dir,
                            "tokens.txt");

    if (!pipeline_test_write_file(model_path)) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("write ctc model asset");
    }
    if (!pipeline_test_write_file(tokenizer_path)) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("write tokenizer asset");
    }

    lrc_pipeline_config_init(&config);
    config.ctc_model_path = model_path;
    config.tokenizer_path = tokenizer_path;
    lrc_pipeline_init(&pipeline, &config);

    if (!lrc_pipeline_validate_ctc_assets(&pipeline, &result)) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("validate ctc assets");
    }
    ASSERT(result.error == LRC_CTC_ASSETS_ERROR_NONE);
    ASSERT(strequal(pipeline.ctc_assets.model_path, model_path));
    ASSERT(strequal(pipeline.ctc_assets.tokenizer_path, tokenizer_path));
    ASSERT(pipeline.ctc_assets.validated);
    ASSERT(pipeline.error == LRC_PIPELINE_ERROR_NONE);

    test_remove_tree(temp_dir);

    return 0;
}

static int32
pipeline_test_ctc_assets_missing_path(void) {
    LrcPipelineConfig config;
    LrcPipeline pipeline;
    LrcCtcAssetsResult result;

    lrc_pipeline_config_init(&config);
    config.tokenizer_path = "tokens.txt";
    lrc_pipeline_init(&pipeline, &config);

    if (lrc_pipeline_validate_ctc_assets(&pipeline, &result)) {
        return pipeline_test_fail("accepted missing ctc model path");
    }
    ASSERT(result.error == LRC_CTC_ASSETS_ERROR_MISSING_MODEL_PATH);
    ASSERT(pipeline.error == LRC_PIPELINE_ERROR_CTC_ASSETS_INVALID);
    ASSERT(!pipeline.ctc_assets.validated);

    return 0;
}

static int32
pipeline_test_ctc_assets_missing_file(void) {
    LrcPipelineConfig config;
    LrcPipeline pipeline;
    LrcCtcAssetsResult result;
    char temp_dir[PATH_MAX];
    char model_path[PATH_MAX];
    char tokenizer_path[PATH_MAX];

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "pipeline_no_ctc");
    pipeline_test_join_path(model_path, SIZEOF(model_path), temp_dir,
                            "missing.onnx");
    pipeline_test_join_path(tokenizer_path, SIZEOF(tokenizer_path), temp_dir,
                            "tokens.txt");

    if (!pipeline_test_write_file(tokenizer_path)) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("write tokenizer asset for missing file");
    }

    lrc_pipeline_config_init(&config);
    config.ctc_model_path = model_path;
    config.tokenizer_path = tokenizer_path;
    lrc_pipeline_init(&pipeline, &config);

    if (lrc_pipeline_validate_ctc_assets(&pipeline, &result)) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("accepted missing ctc model file");
    }
    ASSERT(result.error == LRC_CTC_ASSETS_ERROR_MODEL_NOT_FOUND);
    ASSERT(strequal(result.path, model_path));
    ASSERT(pipeline.error == LRC_PIPELINE_ERROR_CTC_ASSETS_INVALID);
    ASSERT(strequal(pipeline.path, model_path));
    ASSERT(!pipeline.ctc_assets.validated);

    test_remove_tree(temp_dir);

    return 0;
}


static int32
pipeline_test_generate_requires_lyrics_and_output(void) {
    LrcPipelineConfig config;
    LrcPipeline pipeline;
    LrcPipelineGenerateResult result;

    lrc_pipeline_config_init(&config);
    config.existing_vocals_path = "vocals.opus";
    config.ctc_model_path = "ctc.onnx";
    config.tokenizer_path = "tokens.txt";
    lrc_pipeline_init(&pipeline, &config);

    if (lrc_pipeline_generate_lrc(&pipeline, &result)) {
        return pipeline_test_fail("accepted missing lyrics path");
    }
    ASSERT(result.error == LRC_PIPELINE_GENERATE_ERROR_MISSING_LYRICS);

    config.lyrics_text_path = "lyrics.txt";
    lrc_pipeline_init(&pipeline, &config);
    if (lrc_pipeline_generate_lrc(&pipeline, &result)) {
        return pipeline_test_fail("accepted missing output path");
    }
    ASSERT(result.error == LRC_PIPELINE_GENERATE_ERROR_MISSING_OUTPUT);

    return 0;
}

static int32
pipeline_test_generate_from_song_requires_full_config(void) {
    LrcPipelineConfig config;
    LrcPipelineGenerateResult result;

    lrc_pipeline_config_init(&config);
    if (lrc_generate_from_song(&config, &result)) {
        return pipeline_test_fail("accepted missing song path");
    }
    ASSERT(result.error == LRC_PIPELINE_GENERATE_ERROR_MISSING_SONG);

    config.song_path = "song.flac";
    if (lrc_generate_from_song(&config, &result)) {
        return pipeline_test_fail("accepted missing lyrics path");
    }
    ASSERT(result.error == LRC_PIPELINE_GENERATE_ERROR_MISSING_LYRICS);

    config.lyrics_text_path = "lyrics.txt";
    if (lrc_generate_from_song(&config, &result)) {
        return pipeline_test_fail("accepted missing output path");
    }
    ASSERT(result.error == LRC_PIPELINE_GENERATE_ERROR_MISSING_OUTPUT);

    config.output_lrc_path = "out.lrc";
    if (lrc_generate_from_song(&config, &result)) {
        return pipeline_test_fail("accepted missing vocals model path");
    }
    ASSERT(result.error == LRC_PIPELINE_GENERATE_ERROR_MISSING_VOCALS_MODEL);

    config.vocals_model_path = "vocals.onnx";
    if (lrc_generate_from_song(&config, &result)) {
        return pipeline_test_fail("accepted missing CTC model path");
    }
    ASSERT(result.error == LRC_PIPELINE_GENERATE_ERROR_MISSING_CTC_MODEL);

    config.ctc_model_path = "ctc.onnx";
    if (lrc_generate_from_song(&config, &result)) {
        return pipeline_test_fail("accepted missing tokenizer path");
    }
    ASSERT(result.error == LRC_PIPELINE_GENERATE_ERROR_MISSING_TOKENIZER);

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
    if (pipeline_test_ctc_assets_config() != 0) {
        exit(1);
    }
    if (pipeline_test_ctc_assets_validate() != 0) {
        exit(1);
    }
    if (pipeline_test_ctc_assets_missing_path() != 0) {
        exit(1);
    }
    if (pipeline_test_ctc_assets_missing_file() != 0) {
        exit(1);
    }
    if (pipeline_test_generate_requires_lyrics_and_output() != 0) {
        exit(1);
    }
    if (pipeline_test_generate_from_song_requires_full_config() != 0) {
        exit(1);
    }
    if (pipeline_test_optional_maxwell_config() != 0) {
        exit(1);
    }

    return 0;
}

#endif /* TESTING_pipeline */
