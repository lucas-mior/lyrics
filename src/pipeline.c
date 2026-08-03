#if !defined(TESTING_pipeline)
#define TESTING_pipeline 0
#endif

#if !defined(LRC_PIPELINE_ENABLE_GENERATE)
#define LRC_PIPELINE_ENABLE_GENERATE TESTING_pipeline
#endif

#include "lyricsync.h"
#include "cbase.h"
#include "pipeline.h"

static const float LRC_PIPELINE_CLEAR_SILENCE_SECONDS = 1.0f;


typedef struct LrcPipelineLineTimingAudio {
    float *samples;

    int64 sample_count;
    int32 sample_rate;
} LrcPipelineLineTimingAudio;

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
    lrc_lyrics_preprocess_options_init(&config->lyrics_preprocess_options);
    config->ort_session_config.execution_provider =
        ORT_EXECUTION_PROVIDER_AUTO;
    config->ort_session_config.device_id = 0;
    config->ort_session_config.print_info = config->print_info;
    config->ctc_emission_values_kind = LRC_CTC_EMISSION_VALUES_LOGITS;

    return;
}

static bool
lrc_pipeline_debug_dump_enabled(LrcPipeline *pipeline) {
    if (pipeline == NULL) {
        return false;
    }

    return !lrc_pipeline_path_missing(
        pipeline->config.ctc_debug_dump_path
    );
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
    request->ort_session_config = pipeline->config.ort_session_config;
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


typedef struct LrcCtcDebugDumpWriter {
    FILE *file;
    char *path;

    bool ok;
} LrcCtcDebugDumpWriter;

static void
lrc_ctc_debug_dump_writer_init(LrcCtcDebugDumpWriter *writer) {
    if (writer == NULL) {
        return;
    }

    memset64(writer, 0, SIZEOF(*writer));
    writer->ok = true;

    return;
}

static bool
lrc_ctc_debug_dump_writer_open(
    LrcCtcDebugDumpWriter *writer,
    char *path
) {
    if ((writer == NULL) || lrc_pipeline_path_missing(path)) {
        return false;
    }

    lrc_ctc_debug_dump_writer_init(writer);
    writer->path = path;
    if ((writer->file = fopen(path, "wb")) == NULL) {
        writer->ok = false;
        return false;
    }

    return true;
}

static bool
lrc_ctc_debug_dump_writer_close(LrcCtcDebugDumpWriter *writer) {
    bool ok;

    if (writer == NULL) {
        return true;
    }

    ok = writer->ok;
    if (writer->file) {
        if (fclose(writer->file) != 0) {
            ok = false;
        }
    }
    writer->file = NULL;
    writer->ok = ok;

    return ok;
}

static void
lrc_ctc_debug_dump_printf(
    LrcCtcDebugDumpWriter *writer,
    char *format,
    ...
) {
    va_list args;

    if ((writer == NULL) || (writer->file == NULL) || !writer->ok) {
        return;
    }

    va_start(args, format);
    if (vfprintf(writer->file, format, args) < 0) {
        writer->ok = false;
    }
    va_end(args);

    return;
}

static void
lrc_ctc_debug_dump_write_byte(
    LrcCtcDebugDumpWriter *writer,
    char byte
) {
    if ((writer == NULL) || (writer->file == NULL) || !writer->ok) {
        return;
    }

    if (fputc(byte, writer->file) == EOF) {
        writer->ok = false;
    }

    return;
}

static void
lrc_ctc_debug_dump_write_escaped_text(
    LrcCtcDebugDumpWriter *writer,
    char *text,
    int32 text_len
) {
    static char hex[] = "0123456789ABCDEF";

    if ((writer == NULL) || (writer->file == NULL) || !writer->ok) {
        return;
    }
    if (text_len < 0) {
        writer->ok = false;
        return;
    }
    if ((text == NULL) && (text_len > 0)) {
        writer->ok = false;
        return;
    }

    for (int32 i = 0; i < text_len; i += 1) {
        uint8 byte;

        byte = (uint8)text[i];
        switch (byte) {
        case '\\':
            lrc_ctc_debug_dump_printf(writer, "\\\\");
            break;
        case '\t':
            lrc_ctc_debug_dump_printf(writer, "\\t");
            break;
        case '\n':
            lrc_ctc_debug_dump_printf(writer, "\\n");
            break;
        case '\r':
            lrc_ctc_debug_dump_printf(writer, "\\r");
            break;
        default:
            if ((byte < 0x20) || (byte == 0x7f)) {
                lrc_ctc_debug_dump_printf(writer, "\\x");
                lrc_ctc_debug_dump_write_byte(writer, hex[byte >> 4]);
                lrc_ctc_debug_dump_write_byte(writer, hex[byte & 0xf]);
            } else {
                lrc_ctc_debug_dump_write_byte(writer, (char)byte);
            }
            break;
        }
    }

    return;
}

static void
lrc_ctc_debug_dump_write_nullable_text(
    LrcCtcDebugDumpWriter *writer,
    char *text,
    int32 text_len
) {
    if (text == NULL) {
        lrc_ctc_debug_dump_write_escaped_text(writer, STRLIT("<null>"));
        return;
    }

    lrc_ctc_debug_dump_write_escaped_text(writer, text, text_len);

    return;
}

static void
lrc_ctc_debug_dump_write_cstring(
    LrcCtcDebugDumpWriter *writer,
    char *text
) {
    if (text == NULL) {
        lrc_ctc_debug_dump_write_escaped_text(writer, STRLIT("<null>"));
        return;
    }

    lrc_ctc_debug_dump_write_escaped_text(writer, text, strlen32(text));

    return;
}

static void
lrc_ctc_debug_dump_write_section(
    LrcCtcDebugDumpWriter *writer,
    char *name
) {
    lrc_ctc_debug_dump_printf(writer, "\n[");
    lrc_ctc_debug_dump_write_cstring(writer, name);
    lrc_ctc_debug_dump_printf(writer, "]\n");

    return;
}

static void
lrc_ctc_debug_dump_write_key_value(
    LrcCtcDebugDumpWriter *writer,
    char *key,
    char *value
) {
    lrc_ctc_debug_dump_write_cstring(writer, key);
    lrc_ctc_debug_dump_printf(writer, "=");
    lrc_ctc_debug_dump_write_cstring(writer, value);
    lrc_ctc_debug_dump_printf(writer, "\n");

    return;
}

static void
lrc_ctc_debug_dump_write_key_int32(
    LrcCtcDebugDumpWriter *writer,
    char *key,
    int32 value
) {
    lrc_ctc_debug_dump_write_cstring(writer, key);
    lrc_ctc_debug_dump_printf(writer, "=%d\n", value);

    return;
}

static void
lrc_ctc_debug_dump_write_key_int64(
    LrcCtcDebugDumpWriter *writer,
    char *key,
    int64 value
) {
    lrc_ctc_debug_dump_write_cstring(writer, key);
    lrc_ctc_debug_dump_printf(writer, "=%lld\n", value);

    return;
}

static void
lrc_ctc_debug_dump_write_key_double(
    LrcCtcDebugDumpWriter *writer,
    char *key,
    double value
) {
    lrc_ctc_debug_dump_write_cstring(writer, key);
    lrc_ctc_debug_dump_printf(writer, "=%.9g\n", value);

    return;
}

static char *
lrc_pipeline_preprocess_split_size_name(
    enum LrcLyricsPreprocessSplitSize split_size
) {
    switch (split_size) {
    case LRC_LYRICS_PREPROCESS_SPLIT_SIZE_CURRENT:
        return "current";
    case LRC_LYRICS_PREPROCESS_SPLIT_SIZE_WORD:
        return "word";
    case LRC_LYRICS_PREPROCESS_SPLIT_SIZE_CHAR:
        return "char";
    case LRC_LYRICS_PREPROCESS_SPLIT_SIZE_SENTENCE:
        return "sentence";
    default:
        return "invalid";
    }
}

static char *
lrc_pipeline_preprocess_star_frequency_name(
    enum LrcLyricsPreprocessStarFrequency star_frequency
) {
    switch (star_frequency) {
    case LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_NONE:
        return "none";
    case LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_EDGES:
        return "edges";
    case LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_SEGMENT:
        return "segment";
    default:
        return "invalid";
    }
}

static char *
lrc_pipeline_preprocess_romanization_name(
    enum LrcLyricsPreprocessRomanization romanization
) {
    switch (romanization) {
    case LRC_LYRICS_PREPROCESS_ROMANIZATION_OFF:
        return "off";
    case LRC_LYRICS_PREPROCESS_ROMANIZATION_ICU:
        return "icu";
    default:
        return "invalid";
    }
}

static bool
lrc_pipeline_parse_preprocess_split_size(
    LrcPipelineConfig *config,
    char *value
) {
    if ((config == NULL) || (value == NULL)) {
        return false;
    }

    if (strequal(value, "current")) {
        config->lyrics_preprocess_options.split_size =
            LRC_LYRICS_PREPROCESS_SPLIT_SIZE_CURRENT;
        return true;
    }
    if (strequal(value, "word")) {
        config->lyrics_preprocess_options.split_size =
            LRC_LYRICS_PREPROCESS_SPLIT_SIZE_WORD;
        return true;
    }
    if (strequal(value, "char")) {
        config->lyrics_preprocess_options.split_size =
            LRC_LYRICS_PREPROCESS_SPLIT_SIZE_CHAR;
        return true;
    }
    if (strequal(value, "sentence")) {
        config->lyrics_preprocess_options.split_size =
            LRC_LYRICS_PREPROCESS_SPLIT_SIZE_SENTENCE;
        return true;
    }

    error2("--split-size must be current, word, char, or sentence\n");

    return false;
}

static bool
lrc_pipeline_parse_preprocess_star_frequency(
    LrcPipelineConfig *config,
    char *value
) {
    if ((config == NULL) || (value == NULL)) {
        return false;
    }

    if (strequal(value, "none")) {
        config->lyrics_preprocess_options.star_frequency =
            LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_NONE;
        return true;
    }
    if (strequal(value, "edges")) {
        config->lyrics_preprocess_options.star_frequency =
            LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_EDGES;
        return true;
    }
    if (strequal(value, "segment")) {
        config->lyrics_preprocess_options.star_frequency =
            LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_SEGMENT;
        return true;
    }

    error2("--star-frequency must be none, edges, or segment\n");

    return false;
}

static bool
lrc_pipeline_parse_preprocess_romanization(
    LrcPipelineConfig *config,
    char *value
) {
    if ((config == NULL) || (value == NULL)) {
        return false;
    }

    if (strequal(value, "off")) {
        config->lyrics_preprocess_options.romanization =
            LRC_LYRICS_PREPROCESS_ROMANIZATION_OFF;
        return true;
    }
    if (strequal(value, "icu")) {
        config->lyrics_preprocess_options.romanization =
            LRC_LYRICS_PREPROCESS_ROMANIZATION_ICU;
        return true;
    }

    error2("--romanization must be off or icu\n");

    return false;
}

static void
lrc_pipeline_enable_preprocess_romanization(LrcPipelineConfig *config) {
    if (config == NULL) {
        return;
    }

    config->lyrics_preprocess_options.romanization =
        LRC_LYRICS_PREPROCESS_ROMANIZATION_ICU;

    return;
}

static bool
lrc_pipeline_parse_preprocess_language(
    LrcPipelineConfig *config,
    char *value
) {
    int32 value_len;

    if ((config == NULL) || (value == NULL)) {
        return false;
    }

    value_len = strlen32(value);
    if (value_len != 3) {
        error2("--language must be a 3-letter language code\n");
        return false;
    }

    memcpy64(config->lyrics_preprocess_options.language, value, value_len);
    config->lyrics_preprocess_options.language[value_len] = '\0';
    config->lyrics_preprocess_options.language_len = value_len;

    return true;
}

static void
lrc_ctc_debug_dump_write_config(
    LrcCtcDebugDumpWriter *writer,
    LrcPipeline *pipeline
) {
    LrcLyricsPreprocessOptions *options;

    lrc_ctc_debug_dump_write_section(writer, "config");
    if (pipeline == NULL) {
        return;
    }

    options = &pipeline->config.lyrics_preprocess_options;
    lrc_ctc_debug_dump_write_key_value(writer,
                                       "ctc_model_path",
                                       pipeline->ctc_assets.model_path);
    lrc_ctc_debug_dump_write_key_value(writer,
                                       "tokenizer_path",
                                       pipeline->ctc_assets.tokenizer_path);
    lrc_ctc_debug_dump_write_key_value(writer,
                                       "vocals_path",
                                       pipeline->vocals_stage_path);
    lrc_ctc_debug_dump_write_key_value(writer,
                                       "lyrics_path",
                                       pipeline->config.lyrics_text_path);
    lrc_ctc_debug_dump_write_key_value(writer,
                                       "output_path",
                                       pipeline->config.output_lrc_path);
    lrc_ctc_debug_dump_write_key_value(
        writer,
        "split_size",
        lrc_pipeline_preprocess_split_size_name(options->split_size)
    );
    lrc_ctc_debug_dump_write_key_value(
        writer,
        "star_frequency",
        lrc_pipeline_preprocess_star_frequency_name(options->star_frequency)
    );
    lrc_ctc_debug_dump_write_key_value(
        writer,
        "romanization",
        lrc_pipeline_preprocess_romanization_name(options->romanization)
    );
    lrc_ctc_debug_dump_write_key_value(writer, "language", options->language);

    return;
}

static void
lrc_pipeline_debug_dump_target_text(
    LrcLyricsNormalized *normalized,
    char **text,
    int32 *text_len
) {
    *text = NULL;
    *text_len = 0;

    if (normalized == NULL) {
        return;
    }
    if (normalized->target_text_len > 0) {
        *text = normalized->target_text;
        *text_len = normalized->target_text_len;
        return;
    }

    *text = normalized->text;
    *text_len = normalized->text_len;

    return;
}

static LrcCtcToken *
lrc_pipeline_debug_dump_token(
    LrcCtcTokenizer *tokenizer,
    int32 token_id
) {
    if ((tokenizer == NULL) || (tokenizer->tokens == NULL)) {
        return NULL;
    }
    if ((token_id < 0) || (token_id >= tokenizer->token_count)) {
        return NULL;
    }

    return tokenizer->tokens + token_id;
}

static void
lrc_ctc_debug_dump_write_text_section(
    LrcCtcDebugDumpWriter *writer,
    char *section_name,
    char *text,
    int32 text_len
) {
    lrc_ctc_debug_dump_write_section(writer, section_name);
    lrc_ctc_debug_dump_printf(writer, "text=");
    lrc_ctc_debug_dump_write_nullable_text(writer, text, text_len);
    lrc_ctc_debug_dump_printf(writer, "\n");

    return;
}

static void
lrc_ctc_debug_dump_write_target_tokens(
    LrcCtcDebugDumpWriter *writer,
    LrcCtcTokenizer *tokenizer,
    LrcCtcTokenizedText *tokens
) {
    lrc_ctc_debug_dump_write_section(writer, "target_tokens");
    lrc_ctc_debug_dump_printf(
        writer,
        "index\ttoken_id\ttoken_text\tline_index\tsegment_index\t"
        "starts_segment\tnormalized_start\tnormalized_end\n"
    );
    if (tokens == NULL) {
        return;
    }

    for (int32 i = 0; i < tokens->token_count; i += 1) {
        LrcCtcTextToken *text_token;
        LrcCtcToken *token;

        text_token = tokens->tokens + i;
        token = lrc_pipeline_debug_dump_token(tokenizer,
                                              text_token->token_id);
        lrc_ctc_debug_dump_printf(writer,
                                  "%d\t%d\t",
                                  i,
                                  text_token->token_id);
        if (token) {
            lrc_ctc_debug_dump_write_escaped_text(writer,
                                                  token->text,
                                                  token->text_len);
        } else {
            lrc_ctc_debug_dump_write_escaped_text(writer,
                                                  STRLIT("<invalid>"));
        }
        lrc_ctc_debug_dump_printf(writer,
                                  "\t%d\t%d\t%d\t%d\t%d\n",
                                  text_token->line_index,
                                  text_token->segment_index,
                                  text_token->starts_segment ? 1 : 0,
                                  text_token->normalized_start,
                                  text_token->normalized_end);
    }

    return;
}

static void
lrc_ctc_debug_dump_write_text_and_tokens(
    LrcCtcDebugDumpWriter *writer,
    LrcPipeline *pipeline,
    LrcLyricsNormalized *normalized,
    LrcCtcTokenizer *tokenizer,
    LrcCtcTokenizedText *tokens
) {
    char *target_text;
    int32 target_text_len;

    lrc_ctc_debug_dump_printf(writer, "# lrc-ctc-parity-dump-v1\n");
    lrc_ctc_debug_dump_write_config(writer, pipeline);
    lrc_ctc_debug_dump_write_text_section(writer,
                                          "normalized_text",
                                          normalized->text,
                                          normalized->text_len);
    lrc_pipeline_debug_dump_target_text(normalized,
                                        &target_text,
                                        &target_text_len);
    lrc_ctc_debug_dump_write_text_section(writer,
                                          "target_text",
                                          target_text,
                                          target_text_len);
    lrc_ctc_debug_dump_write_target_tokens(writer, tokenizer, tokens);

    return;
}

static void
lrc_ctc_debug_dump_write_audio_model(
    LrcCtcDebugDumpWriter *writer,
    LrcPipeline *pipeline,
    LrcCtcAudio *audio,
    LrcCtcModelInput *input,
    LrcCtcEmissions *emissions,
    LrcCtcTokenizer *tokenizer,
    int32 star_token_id
) {
    double frame_duration_seconds;

    lrc_ctc_debug_dump_write_section(writer, "frames");

    if ((pipeline == NULL) || (audio == NULL) || (input == NULL)
        || (emissions == NULL) || (tokenizer == NULL)) {
        lrc_ctc_debug_dump_write_key_value(writer, "status", "missing_data");
        return;
    }

    frame_duration_seconds = input->stride_ms/1000.0;

    lrc_ctc_debug_dump_write_key_int32(writer,
                                       "audio_sample_rate",
                                       audio->sample_rate);
    lrc_ctc_debug_dump_write_key_int64(writer,
                                       "audio_sample_count",
                                       audio->sample_count);
    lrc_ctc_debug_dump_write_key_int32(writer,
                                       "model_sample_rate",
                                       input->sample_rate);
    lrc_ctc_debug_dump_write_key_int32(writer,
                                       "inputs_to_logits_ratio",
                                       input->inputs_to_logits_ratio);
    lrc_ctc_debug_dump_write_key_double(writer,
                                        "stride_ms",
                                        input->stride_ms);
    lrc_ctc_debug_dump_write_key_double(writer,
                                        "frame_duration_seconds",
                                        frame_duration_seconds);
    lrc_ctc_debug_dump_write_key_int64(writer,
                                       "emission_frame_count",
                                       emissions->frame_count);
    lrc_ctc_debug_dump_write_key_int64(writer,
                                       "emission_vocabulary_size",
                                       emissions->vocabulary_size);
    lrc_ctc_debug_dump_write_key_int32(writer,
                                       "tokenizer_token_count",
                                       tokenizer->token_count);
    lrc_ctc_debug_dump_write_key_int32(writer,
                                       "blank_token_id",
                                       tokenizer->blank_id);
    lrc_ctc_debug_dump_write_key_int32(writer,
                                       "star_token_id",
                                       star_token_id);
    lrc_ctc_debug_dump_write_key_int64(writer,
                                       "chunk_count",
                                       input->chunk_count);
    lrc_ctc_debug_dump_write_key_int64(writer,
                                       "row_sample_count",
                                       input->row_sample_count);
    lrc_ctc_debug_dump_write_key_int32(
        writer,
        "window_seconds",
        pipeline->config.ctc_model_config.window_seconds
    );
    lrc_ctc_debug_dump_write_key_int32(
        writer,
        "context_seconds",
        pipeline->config.ctc_model_config.context_seconds
    );

    return;
}

static void
lrc_ctc_debug_dump_write_segment_token_text(
    LrcCtcDebugDumpWriter *writer,
    LrcCtcTokenizer *tokenizer,
    LrcCtcPathSegment *segment
) {
    LrcCtcToken *token;

    if (segment == NULL) {
        lrc_ctc_debug_dump_write_escaped_text(writer, STRLIT("<invalid>"));
        return;
    }
    if (segment->is_star) {
        lrc_ctc_debug_dump_write_escaped_text(writer, STRLIT("<star>"));
        return;
    }

    token = lrc_pipeline_debug_dump_token(tokenizer, segment->token_id);
    if (token) {
        lrc_ctc_debug_dump_write_escaped_text(writer,
                                              token->text,
                                              token->text_len);
    } else {
        lrc_ctc_debug_dump_write_escaped_text(writer, STRLIT("<invalid>"));
    }

    return;
}

static void
lrc_ctc_debug_dump_write_path_segments(
    LrcCtcDebugDumpWriter *writer,
    LrcCtcTokenizer *tokenizer,
    LrcCtcPathSegments *segments
) {
    lrc_ctc_debug_dump_write_section(writer, "merged_path_segments");
    lrc_ctc_debug_dump_printf(
        writer,
        "index\ttoken_index\ttoken_id\ttoken_text\tstart_frame\t"
        "end_frame\tstart_seconds\tend_seconds\tscore\tis_blank\tis_star\n"
    );
    if (segments == NULL) {
        return;
    }

    for (int64 i = 0; i < segments->segment_count; i += 1) {
        LrcCtcPathSegment *segment;
        int32 is_blank;
        int32 is_star;

        segment = segments->segments + i;
        is_blank = 0;
        is_star = 0;
        if (segment->is_blank) {
            is_blank = 1;
        }
        if (segment->is_star) {
            is_star = 1;
        }
        lrc_ctc_debug_dump_printf(writer,
                                  "%lld\t%lld\t%d\t",
                                  i,
                                  segment->token_index,
                                  segment->token_id);
        lrc_ctc_debug_dump_write_segment_token_text(writer,
                                                    tokenizer,
                                                    segment);
        lrc_ctc_debug_dump_printf(
            writer,
            "\t%lld\t%lld\t%.9g\t%.9g\t%.9g\t%d\t%d\n",
            segment->start_frame,
            segment->end_frame,
            (double)segment->start_seconds,
            (double)segment->end_seconds,
            (double)segment->score,
            is_blank,
            is_star
        );
    }

    return;
}


static void
lrc_ctc_debug_dump_write_word_span_text(
    LrcCtcDebugDumpWriter *writer,
    LrcLyricsNormalized *normalized,
    LrcCtcWordSpan *word
) {
    int32 text_len;

    if ((normalized == NULL) || (word == NULL)
        || (normalized->text == NULL)
        || (word->normalized_start < 0)
        || (word->normalized_end < word->normalized_start)
        || (word->normalized_end > normalized->text_len)) {
        lrc_ctc_debug_dump_write_escaped_text(writer, STRLIT("<invalid>"));
        return;
    }

    text_len = word->normalized_end - word->normalized_start;
    lrc_ctc_debug_dump_write_escaped_text(writer,
                                          normalized->text
                                          + word->normalized_start,
                                          text_len);

    return;
}

static void
lrc_ctc_debug_dump_write_word_spans(
    LrcCtcDebugDumpWriter *writer,
    char *section_name,
    LrcLyricsNormalized *normalized,
    LrcCtcWordSpans *word_spans
) {
    lrc_ctc_debug_dump_write_section(writer, section_name);
    lrc_ctc_debug_dump_printf(
        writer,
        "index\tline_index\tword_index\ttext\tnormalized_start\t"
        "normalized_end\ttoken_start_index\ttoken_end_index\t"
        "span_start_index\tspan_end_index\tstart_seconds\t"
        "end_seconds\tscore\n"
    );
    if (word_spans == NULL) {
        return;
    }

    for (int64 i = 0; i < word_spans->span_count; i += 1) {
        LrcCtcWordSpan *word;

        word = word_spans->spans + i;
        lrc_ctc_debug_dump_printf(writer,
                                  "%lld\t%d\t%lld\t",
                                  i,
                                  word->line_index,
                                  word->word_index);
        lrc_ctc_debug_dump_write_word_span_text(writer, normalized, word);
        lrc_ctc_debug_dump_printf(
            writer,
            "\t%d\t%d\t%lld\t%lld\t%lld\t%lld\t%.9g\t%.9g\t%.9g\n",
            word->normalized_start,
            word->normalized_end,
            word->token_start_index,
            word->token_end_index,
            word->span_start_index,
            word->span_end_index,
            (double)word->start_seconds,
            (double)word->end_seconds,
            (double)word->score
        );
    }

    return;
}

static bool
lrc_pipeline_debug_dump_check_writer(
    LrcPipeline *pipeline,
    LrcCtcDebugDumpWriter *writer,
    LrcPipelineGenerateResult *result
) {
    if ((writer == NULL) || writer->ok) {
        return true;
    }

    lrc_pipeline_generate_result_set(
        result,
        LRC_PIPELINE_GENERATE_ERROR_LRC_WRITE_FAILED,
        "could not write CTC debug dump",
        pipeline->config.ctc_debug_dump_path
    );

    return false;
}

static bool
lrc_pipeline_debug_dump_write_active_word_spans(
    LrcPipeline *pipeline,
    LrcCtcDebugDumpWriter *writer,
    LrcCtcPath *path,
    LrcCtcEmissions *emissions,
    LrcCtcTokenizedText *tokens,
    LrcLyricsNormalized *normalized,
    float frame_duration_seconds,
    LrcCtcAlignResult *align_result,
    LrcPipelineGenerateResult *result
) {
    LrcCtcTokenSpans active_token_spans;
    LrcCtcWordSpans active_word_spans;
    bool ok;

    if (!lrc_pipeline_debug_dump_enabled(pipeline)) {
        return true;
    }

    lrc_ctc_token_spans_init(&active_token_spans);
    lrc_ctc_word_spans_init(&active_word_spans);

    ok = lrc_ctc_path_to_token_spans(path,
                                     emissions,
                                     frame_duration_seconds,
                                     &active_token_spans,
                                     align_result);
    if (ok) {
        ok = lrc_ctc_token_spans_to_word_spans(&active_token_spans,
                                               tokens,
                                               normalized,
                                               &active_word_spans,
                                               align_result);
    }
    if (ok) {
        lrc_ctc_debug_dump_write_word_spans(writer,
                                            "word_spans_before_padding",
                                            normalized,
                                            &active_word_spans);
    }

    lrc_ctc_word_spans_destroy(&active_word_spans);
    lrc_ctc_token_spans_destroy(&active_token_spans);

    if (!ok) {
        char *message;

        message = "could not build active CTC word spans";
        if (align_result) {
            message = align_result->message;
        }
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_ALIGNMENT_FAILED,
            message,
            NULL
        );
        return false;
    }

    return lrc_pipeline_debug_dump_check_writer(pipeline, writer, result);
}

static bool
lrc_pipeline_debug_dump_write_padded_word_spans(
    LrcPipeline *pipeline,
    LrcCtcDebugDumpWriter *writer,
    LrcLyricsNormalized *normalized,
    LrcCtcWordSpans *word_spans,
    LrcPipelineGenerateResult *result
) {
    if (!lrc_pipeline_debug_dump_enabled(pipeline)) {
        return true;
    }

    lrc_ctc_debug_dump_write_word_spans(writer,
                                        "word_spans_after_padding",
                                        normalized,
                                        word_spans);

    return lrc_pipeline_debug_dump_check_writer(pipeline, writer, result);
}

static bool
lrc_pipeline_debug_dump_write_path_segments(
    LrcPipeline *pipeline,
    LrcCtcDebugDumpWriter *writer,
    LrcCtcPath *path,
    LrcCtcEmissions *emissions,
    LrcCtcTokenizer *tokenizer,
    float frame_duration_seconds,
    LrcCtcAlignResult *align_result,
    LrcPipelineGenerateResult *result
) {
    LrcCtcPathSegments segments;

    if (!lrc_pipeline_debug_dump_enabled(pipeline)) {
        return true;
    }

    lrc_ctc_path_segments_init(&segments);
    if (!lrc_ctc_path_to_segments(path,
                                  emissions,
                                  frame_duration_seconds,
                                  &segments,
                                  align_result)) {
        char *message;

        message = "could not build merged CTC path segments";
        if (align_result) {
            message = align_result->message;
        }
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_ALIGNMENT_FAILED,
            message,
            NULL
        );
        lrc_ctc_path_segments_destroy(&segments);
        return false;
    }

    lrc_ctc_debug_dump_write_path_segments(writer, tokenizer, &segments);
    lrc_ctc_path_segments_destroy(&segments);
    if (!writer->ok) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_LRC_WRITE_FAILED,
            "could not write CTC debug dump",
            pipeline->config.ctc_debug_dump_path
        );
        return false;
    }

    return true;
}

static bool
lrc_pipeline_debug_dump_open_and_write_text(
    LrcPipeline *pipeline,
    LrcCtcDebugDumpWriter *writer,
    LrcLyricsNormalized *normalized,
    LrcCtcTokenizer *tokenizer,
    LrcCtcTokenizedText *tokens,
    LrcPipelineGenerateResult *result
) {
    if (!lrc_pipeline_debug_dump_enabled(pipeline)) {
        return true;
    }
    if (!lrc_ctc_debug_dump_writer_open(
        writer,
        pipeline->config.ctc_debug_dump_path
    )) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_LRC_WRITE_FAILED,
            "could not open CTC debug dump",
            pipeline->config.ctc_debug_dump_path
        );
        return false;
    }

    lrc_ctc_debug_dump_write_text_and_tokens(writer,
                                             pipeline,
                                             normalized,
                                             tokenizer,
                                             tokens);
    if (!writer->ok) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_LRC_WRITE_FAILED,
            "could not write CTC debug dump",
            pipeline->config.ctc_debug_dump_path
        );
        return false;
    }

    return true;
}

static bool
lrc_pipeline_trellis_score_forward(
    LrcPipeline *pipeline,
    LrcCtcTrellis *trellis,
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    bool *target_segment_starts,
    int64 target_token_count,
    int32 blank_token_id,
    int32 star_token_id,
    LrcCtcAlignResult *align_result,
    LrcPipelineGenerateResult *result
) {
    bool ok;

    switch (pipeline->config.lyrics_preprocess_options.star_frequency) {
    case LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_NONE:
        ok = lrc_ctc_trellis_score_forward(trellis,
                                           emissions,
                                           target_token_ids,
                                           target_token_count,
                                           blank_token_id,
                                           align_result);
        break;
    case LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_EDGES:
        ok = lrc_ctc_trellis_score_forward_with_edge_stars(
            trellis,
            emissions,
            target_token_ids,
            target_token_count,
            blank_token_id,
            star_token_id,
            align_result
        );
        break;
    case LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_SEGMENT:
        ok = lrc_ctc_trellis_score_forward_with_segment_stars(
            trellis,
            emissions,
            target_token_ids,
            target_segment_starts,
            target_token_count,
            blank_token_id,
            star_token_id,
            align_result
        );
        break;
    default:
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_ALIGNMENT_FAILED,
            "star frequency is invalid",
            NULL
        );
        return false;
    }

    if (!ok) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_ALIGNMENT_FAILED,
            align_result->message,
            NULL
        );
        if (result) {
            result->frame_index = align_result->frame_index;
            result->token_index = align_result->token_index;
        }
        return false;
    }

    return true;
}

static bool
lrc_pipeline_trellis_backtrack(
    LrcPipeline *pipeline,
    LrcCtcTrellis *trellis,
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    bool *target_segment_starts,
    int64 target_token_count,
    int32 blank_token_id,
    int32 star_token_id,
    LrcCtcPath *path,
    LrcCtcAlignResult *align_result,
    LrcPipelineGenerateResult *result
) {
    bool ok;

    switch (pipeline->config.lyrics_preprocess_options.star_frequency) {
    case LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_NONE:
        ok = lrc_ctc_trellis_backtrack(trellis,
                                       emissions,
                                       target_token_ids,
                                       target_token_count,
                                       blank_token_id,
                                       path,
                                       align_result);
        break;
    case LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_EDGES:
        ok = lrc_ctc_trellis_backtrack_with_edge_stars(trellis,
                                                       emissions,
                                                       target_token_ids,
                                                       target_token_count,
                                                       blank_token_id,
                                                       star_token_id,
                                                       path,
                                                       align_result);
        break;
    case LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_SEGMENT:
        ok = lrc_ctc_trellis_backtrack_with_segment_stars(
            trellis,
            emissions,
            target_token_ids,
            target_segment_starts,
            target_token_count,
            blank_token_id,
            star_token_id,
            path,
            align_result
        );
        break;
    default:
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_ALIGNMENT_FAILED,
            "star frequency is invalid",
            NULL
        );
        return false;
    }

    if (!ok) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_ALIGNMENT_FAILED,
            align_result->message,
            NULL
        );
        if (result) {
            result->frame_index = align_result->frame_index;
            result->token_index = align_result->token_index;
        }
        return false;
    }

    return true;
}

static bool
lrc_pipeline_path_to_padded_token_spans(
    LrcPipeline *pipeline,
    LrcCtcPath *path,
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    bool *target_segment_starts,
    int64 target_token_count,
    int32 star_token_id,
    float frame_duration_seconds,
    LrcCtcTokenSpans *token_spans,
    LrcCtcAlignResult *align_result,
    LrcPipelineGenerateResult *result
) {
    bool ok;

    switch (pipeline->config.lyrics_preprocess_options.star_frequency) {
    case LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_NONE:
        ok = lrc_ctc_path_to_padded_token_spans(path,
                                                emissions,
                                                target_token_ids,
                                                target_token_count,
                                                frame_duration_seconds,
                                                token_spans,
                                                align_result);
        break;
    case LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_EDGES:
        ok = lrc_ctc_path_to_padded_token_spans_with_edge_stars(
            path,
            emissions,
            target_token_ids,
            target_token_count,
            star_token_id,
            frame_duration_seconds,
            token_spans,
            align_result
        );
        break;
    case LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_SEGMENT:
        ok = lrc_ctc_path_to_padded_token_spans_with_segment_stars(
            path,
            emissions,
            target_token_ids,
            target_segment_starts,
            target_token_count,
            star_token_id,
            frame_duration_seconds,
            token_spans,
            align_result
        );
        break;
    default:
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_ALIGNMENT_FAILED,
            "star frequency is invalid",
            NULL
        );
        return false;
    }

    if (!ok) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_ALIGNMENT_FAILED,
            align_result->message,
            NULL
        );
        if (result) {
            result->frame_index = align_result->frame_index;
            result->token_index = align_result->token_index;
        }
        return false;
    }

    return true;
}

static void
lrc_pipeline_line_timing_audio_init(
    LrcPipelineLineTimingAudio *audio
) {
    if (audio == NULL) {
        return;
    }

    memset64(audio, 0, SIZEOF(*audio));

    return;
}

static bool
lrc_pipeline_line_timing_audio_from_ctc_audio(
    LrcPipelineLineTimingAudio *line_audio,
    LrcCtcAudio *audio,
    LrcPipelineGenerateResult *result
) {
    if (line_audio) {
        lrc_pipeline_line_timing_audio_init(line_audio);
    }
    if ((line_audio == NULL) || (audio == NULL) || (audio->samples == NULL)
        || (audio->sample_count <= 0) || (audio->sample_rate <= 0)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_INVALID_ARGUMENT,
            "line timing audio context is invalid",
            NULL
        );
        return false;
    }

    line_audio->samples = audio->samples;
    line_audio->sample_count = audio->sample_count;
    line_audio->sample_rate = audio->sample_rate;

    return true;
}

static bool
lrc_pipeline_output_line_set_timestamped(
    LrcOutputLine *line,
    char *text,
    int32 text_len,
    float seconds,
    LrcPipelineGenerateResult *result
) {
    LrcFormatResult format_result;
    int32 hundredths;

    if (!lrc_timestamp_hundredths_from_seconds(seconds,
                                               &hundredths,
                                               &format_result)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_OUTPUT_LINES_FAILED,
            "could not format LRC timestamp",
            NULL
        );
        return false;
    }

    line->text = text;
    line->text_len = text_len;
    line->timestamp_hundredths = hundredths;
    line->kind = LRC_OUTPUT_LINE_KIND_TIMESTAMPED;

    return true;
}

static LrcCtcLineTimestamp *
lrc_pipeline_next_timestamped_line(
    LrcCtcLineTimestamps *timestamps,
    int64 start_index
) {
    for (int64 i = start_index; i < timestamps->line_count; i += 1) {
        LrcCtcLineTimestamp *timestamp;

        timestamp = timestamps->lines + i;
        if (timestamp->kind == LRC_CTC_LINE_TIMESTAMP_KIND_TIMESTAMPED) {
            return timestamp;
        }
    }

    return NULL;
}

static bool
lrc_pipeline_timestamp_needs_clear_line(
    LrcCtcLineTimestamps *timestamps,
    int64 timestamp_index
) {
    LrcCtcLineTimestamp *current;
    LrcCtcLineTimestamp *next;
    float gap_seconds;

    current = timestamps->lines + timestamp_index;
    if (current->kind != LRC_CTC_LINE_TIMESTAMP_KIND_TIMESTAMPED) {
        return false;
    }
    if (current->end_seconds <= current->start_seconds) {
        return false;
    }

    next = lrc_pipeline_next_timestamped_line(timestamps,
                                              timestamp_index + 1);
    if (next == NULL) {
        return false;
    }

    gap_seconds = next->start_seconds - current->end_seconds;

    return gap_seconds >= LRC_PIPELINE_CLEAR_SILENCE_SECONDS;
}

static bool
lrc_pipeline_output_lines_from_timestamps(
    LrcLyrics *lyrics,
    LrcCtcLineTimestamps *timestamps,
    LrcOutputLine *lines,
    int64 line_cap,
    int32 *line_count,
    LrcPipelineGenerateResult *result
) {
    int64 out_index;

    if (line_count) {
        *line_count = 0;
    }
    if ((lyrics == NULL) || (timestamps == NULL) || (lines == NULL)
        || (line_count == NULL)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_INVALID_ARGUMENT,
            "LRC output line conversion arguments are invalid",
            NULL
        );
        return false;
    }
    if ((timestamps->line_count < 0)
        || (timestamps->line_count > INT32_MAX)
        || (line_cap < timestamps->line_count)
        || (line_cap > INT32_MAX)) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_TOO_LARGE,
            "too many LRC output lines",
            NULL
        );
        return false;
    }

    out_index = 0;
    for (int64 i = 0; i < timestamps->line_count; i += 1) {
        LrcCtcLineTimestamp *timestamp;
        LrcLyricsLine *lyrics_line;

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
        if (out_index >= line_cap) {
            lrc_pipeline_generate_result_set(
                result,
                LRC_PIPELINE_GENERATE_ERROR_TOO_LARGE,
                "too many LRC output lines",
                NULL
            );
            return false;
        }

        lyrics_line = lyrics->lines + timestamp->line_index;
        switch (timestamp->kind) {
        case LRC_CTC_LINE_TIMESTAMP_KIND_TIMESTAMPED:
            if (!lrc_pipeline_output_line_set_timestamped(
                lines + out_index,
                lyrics_line->text,
                lyrics_line->text_len,
                timestamp->start_seconds,
                result
            )) {
                return false;
            }
            out_index += 1;
            if (!lrc_pipeline_timestamp_needs_clear_line(timestamps, i)) {
                break;
            }
            if (out_index >= line_cap) {
                lrc_pipeline_generate_result_set(
                    result,
                    LRC_PIPELINE_GENERATE_ERROR_TOO_LARGE,
                    "too many LRC output lines",
                    NULL
                );
                return false;
            }
            if (!lrc_pipeline_output_line_set_timestamped(
                lines + out_index,
                "",
                0,
                timestamp->end_seconds,
                result
            )) {
                return false;
            }
            out_index += 1;
            break;
        case LRC_CTC_LINE_TIMESTAMP_KIND_BLANK:
            lines[out_index].text = lyrics_line->text;
            lines[out_index].text_len = lyrics_line->text_len;
            lines[out_index].timestamp_hundredths = -1;
            lines[out_index].kind = LRC_OUTPUT_LINE_KIND_BLANK;
            out_index += 1;
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

    *line_count = (int32)out_index;

    return true;
}

static bool
lrc_pipeline_generate_targets(
    LrcCtcTokenizedText *tokens,
    int32 **target_token_ids,
    bool **target_segment_starts,
    int64 *target_token_count,
    LrcPipelineGenerateResult *result
) {
    int64 count;

    if ((tokens == NULL) || (target_token_ids == NULL)
        || (target_segment_starts == NULL)
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
    if ((count > INT64_MAX/SIZEOF(**target_token_ids))
        || (count > INT64_MAX/SIZEOF(**target_segment_starts))) {
        lrc_pipeline_generate_result_set(
            result,
            LRC_PIPELINE_GENERATE_ERROR_TOO_LARGE,
            "CTC target token allocation is too large",
            NULL
        );
        return false;
    }

    *target_token_ids = malloc2(count*SIZEOF(**target_token_ids));
    *target_segment_starts = malloc2(count*SIZEOF(**target_segment_starts));
    *target_token_count = count;
    for (int64 i = 0; i < count; i += 1) {
        (*target_token_ids)[i] = tokens->tokens[i].token_id;
        (*target_segment_starts)[i] = tokens->tokens[i].starts_segment;
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
    OrtSessionConfig ort_session_config;
    LrcCtcInferenceResult inference_result;
    LrcCtcEmissions emissions;
    LrcCtcAlignResult align_result;
    LrcCtcTrellis trellis;
    LrcCtcPath path;
    LrcCtcTokenSpans token_spans;
    LrcCtcWordSpans word_spans;
    LrcCtcLineTimestamps line_timestamps;
    LrcPipelineLineTimingAudio line_audio;
    LrcCtcDebugDumpWriter debug_dump;
    LrcOutputLine *output_lines;
    LrcWriteResult write_result;
    int32 *target_token_ids;
    bool *target_segment_starts;
    int64 target_token_count;
    int64 output_line_cap;
    int32 output_line_count;
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
    lrc_pipeline_line_timing_audio_init(&line_audio);
    lrc_ctc_debug_dump_writer_init(&debug_dump);

    output_lines = NULL;
    target_token_ids = NULL;
    target_segment_starts = NULL;
    target_token_count = 0;
    output_line_cap = 0;
    output_line_count = 0;
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
    if (ok && !lrc_lyrics_normalize_with_options(
        &lyrics,
        &normalized,
        &pipeline->config.lyrics_preprocess_options
    )) {
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
    if (ok && !lrc_pipeline_debug_dump_open_and_write_text(pipeline,
                                                           &debug_dump,
                                                           &normalized,
                                                           &tokenizer,
                                                           &tokens,
                                                           result)) {
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
    ort_session_config = pipeline->config.ort_session_config;
    ort_session_config.print_info = pipeline->config.print_info;
    if (ok && !lrc_ctc_onnx_inference_load(
        &onnx,
        pipeline->ctc_assets.model_path,
        &ort_session_config,
        &inference_result
    )) {
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
                                             &target_segment_starts,
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
    if (ok && lrc_pipeline_debug_dump_enabled(pipeline)) {
        lrc_ctc_debug_dump_write_audio_model(&debug_dump,
                                             pipeline,
                                             &audio,
                                             &input,
                                             &emissions,
                                             &tokenizer,
                                             star_token_id);
        if (!debug_dump.ok) {
            lrc_pipeline_generate_result_set(
                result,
                LRC_PIPELINE_GENERATE_ERROR_LRC_WRITE_FAILED,
                "could not write CTC debug dump",
                pipeline->config.ctc_debug_dump_path
            );
            ok = false;
        }
    }
    if (ok && !lrc_pipeline_trellis_score_forward(pipeline,
                                                   &trellis,
                                                   &emissions,
                                                   target_token_ids,
                                                   target_segment_starts,
                                                   target_token_count,
                                                   tokenizer.blank_id,
                                                   star_token_id,
                                                   &align_result,
                                                   result)) {
        ok = false;
    }
    if (ok && !lrc_pipeline_trellis_backtrack(pipeline,
                                              &trellis,
                                              &emissions,
                                              target_token_ids,
                                              target_segment_starts,
                                              target_token_count,
                                              tokenizer.blank_id,
                                              star_token_id,
                                              &path,
                                              &align_result,
                                              result)) {
        ok = false;
    }

    frame_duration_seconds = (float)(input.stride_ms/1000.0);
    if (ok && !lrc_pipeline_debug_dump_write_path_segments(
        pipeline,
        &debug_dump,
        &path,
        &emissions,
        &tokenizer,
        frame_duration_seconds,
        &align_result,
        result
    )) {
        ok = false;
    }
    if (ok && !lrc_pipeline_debug_dump_write_active_word_spans(
        pipeline,
        &debug_dump,
        &path,
        &emissions,
        &tokens,
        &normalized,
        frame_duration_seconds,
        &align_result,
        result
    )) {
        ok = false;
    }
    if (ok && !lrc_pipeline_path_to_padded_token_spans(
        pipeline,
        &path,
        &emissions,
        target_token_ids,
        target_segment_starts,
        target_token_count,
        star_token_id,
        frame_duration_seconds,
        &token_spans,
        &align_result,
        result
    )) {
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
    if (ok && !lrc_pipeline_debug_dump_write_padded_word_spans(
        pipeline,
        &debug_dump,
        &normalized,
        &word_spans,
        result
    )) {
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
    if (ok && !lrc_pipeline_line_timing_audio_from_ctc_audio(&line_audio,
                                                             &audio,
                                                             result)) {
        ok = false;
    }
    if (ok) {
        output_line_cap = 2*line_timestamps.line_count;
        if ((line_timestamps.line_count < 0)
            || (line_timestamps.line_count > INT32_MAX/2)
            || (output_line_cap > INT64_MAX/SIZEOF(*output_lines))) {
            lrc_pipeline_generate_result_set(
                result,
                LRC_PIPELINE_GENERATE_ERROR_TOO_LARGE,
                "LRC output line allocation is too large",
                NULL
            );
            ok = false;
        }
    }
    if (ok) {
        output_lines = malloc2(output_line_cap*SIZEOF(*output_lines));
        if (!lrc_pipeline_output_lines_from_timestamps(&lyrics,
                                                       &line_timestamps,
                                                       output_lines,
                                                       output_line_cap,
                                                       &output_line_count,
                                                       result)) {
            ok = false;
        }
    }
    if (ok && !lrc_write_output_file(pipeline->config.output_lrc_path,
                                     output_lines,
                                     output_line_count,
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

    if (debug_dump.file) {
        if (!lrc_ctc_debug_dump_writer_close(&debug_dump) && ok) {
            lrc_pipeline_generate_result_set(
                result,
                LRC_PIPELINE_GENERATE_ERROR_LRC_WRITE_FAILED,
                "could not close CTC debug dump",
                pipeline->config.ctc_debug_dump_path
            );
            ok = false;
        }
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
        free2(output_lines, output_line_cap*SIZEOF(*output_lines));
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

    if (result) {
        result->error = LRC_LYRICS_LOAD_ERROR_OPEN_FAILED;
        result->message = "lyrics load stub failed";
        result->path = path;
        result->byte_offset = -1;
    }

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
lrc_lyrics_normalize_with_options(
    LrcLyrics *lyrics,
    LrcLyricsNormalized *normalized,
    LrcLyricsPreprocessOptions *options
) {
    (void)lyrics;
    (void)normalized;
    (void)options;

    return false;
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
    OrtSessionConfig *session_config,
    LrcCtcInferenceResult *result
) {
    (void)onnx;
    (void)model_path;
    (void)session_config;
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
lrc_ctc_trellis_score_forward_with_segment_stars(
    LrcCtcTrellis *trellis,
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    bool *target_segment_starts,
    int64 target_token_count,
    int32 blank_token_id,
    int32 star_token_id,
    LrcCtcAlignResult *result
) {
    (void)trellis;
    (void)emissions;
    (void)target_token_ids;
    (void)target_segment_starts;
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


static bool
lrc_ctc_trellis_backtrack_with_segment_stars(
    LrcCtcTrellis *trellis,
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    bool *target_segment_starts,
    int64 target_token_count,
    int32 blank_token_id,
    int32 star_token_id,
    LrcCtcPath *path,
    LrcCtcAlignResult *result
) {
    (void)trellis;
    (void)emissions;
    (void)target_token_ids;
    (void)target_segment_starts;
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

static void
lrc_ctc_path_segments_init(LrcCtcPathSegments *segments) {
    memset64(segments, 0, SIZEOF(*segments));

    return;
}

static void
lrc_ctc_path_segments_destroy(LrcCtcPathSegments *segments) {
    lrc_pipeline_test_noop(segments);

    return;
}

static bool
lrc_ctc_path_to_segments(
    LrcCtcPath *path,
    LrcCtcEmissions *emissions,
    float frame_duration_seconds,
    LrcCtcPathSegments *segments,
    LrcCtcAlignResult *result
) {
    (void)path;
    (void)emissions;
    (void)frame_duration_seconds;
    (void)segments;
    (void)result;

    return false;
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

static bool
lrc_ctc_path_to_padded_token_spans(
    LrcCtcPath *path,
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    int64 target_token_count,
    float frame_duration_seconds,
    LrcCtcTokenSpans *spans,
    LrcCtcAlignResult *result
) {
    (void)path;
    (void)emissions;
    (void)target_token_ids;
    (void)target_token_count;
    (void)frame_duration_seconds;
    (void)spans;
    (void)result;

    return false;
}

static bool
lrc_ctc_path_to_padded_token_spans_with_edge_stars(
    LrcCtcPath *path,
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    int64 target_token_count,
    int32 star_token_id,
    float frame_duration_seconds,
    LrcCtcTokenSpans *spans,
    LrcCtcAlignResult *result
) {
    (void)path;
    (void)emissions;
    (void)target_token_ids;
    (void)target_token_count;
    (void)star_token_id;
    (void)frame_duration_seconds;
    (void)spans;
    (void)result;

    return false;
}

static bool
lrc_ctc_path_to_padded_token_spans_with_segment_stars(
    LrcCtcPath *path,
    LrcCtcEmissions *emissions,
    int32 *target_token_ids,
    bool *target_segment_starts,
    int64 target_token_count,
    int32 star_token_id,
    float frame_duration_seconds,
    LrcCtcTokenSpans *spans,
    LrcCtcAlignResult *result
) {
    (void)path;
    (void)emissions;
    (void)target_token_ids;
    (void)target_segment_starts;
    (void)target_token_count;
    (void)star_token_id;
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
    double rounded;

    (void)result;
    if (timestamp_hundredths == NULL) {
        return false;
    }
    if (!isfinite(seconds) || (seconds < 0.0f)) {
        return false;
    }

    rounded = (double)seconds*100.0 + 0.5;
    if (rounded > (double)INT32_MAX) {
        return false;
    }

    *timestamp_hundredths = (int32)rounded;

    return true;
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


static bool
pipeline_test_file_contains(
    char *path,
    char *needle,
    int32 needle_len
) {
    char *text;
    int32 text_len;
    bool found;

    text = read_entire_file(path, &text_len);
    found = memmem64(text, text_len, needle, needle_len) != NULL;
    free2(text, text_len + 1);

    return found;
}


static bool
pipeline_test_output_text_equal(LrcOutputLine *line, char *text, int32 len) {
    return strequal2(line->text, line->text_len, text, len);
}

static int32
pipeline_test_line_timing_audio_available(void) {
    LrcCtcAudio audio;
    LrcPipelineLineTimingAudio line_audio;
    LrcPipelineGenerateResult result;
    float samples[] = {
        0.25f,
        -0.50f,
        0.00f,
        0.50f,
    };

    lrc_ctc_audio_init(&audio);
    lrc_pipeline_line_timing_audio_init(&line_audio);
    lrc_pipeline_generate_result_init(&result);

    audio.samples = samples;
    audio.sample_count = LENGTH(samples);
    audio.sample_rate = 16000;
    audio.channel_count = 1;
    audio.duration_seconds = (double)audio.sample_count
                             /(double)audio.sample_rate;

    if (!lrc_pipeline_line_timing_audio_from_ctc_audio(&line_audio,
                                                       &audio,
                                                       &result)) {
        return pipeline_test_fail("line timing audio context");
    }
    ASSERT(result.error == LRC_PIPELINE_GENERATE_ERROR_NONE);
    ASSERT(line_audio.samples == samples);
    ASSERT(line_audio.sample_count == LENGTH(samples));
    ASSERT(line_audio.sample_rate == 16000);
    ASSERT(line_audio.samples[1] == -0.50f);

    return 0;
}

static int32
pipeline_test_line_timestamp_clear_case(
    char *name,
    float first_end_seconds,
    float second_start_seconds,
    bool expect_clear,
    int32 expected_clear_hundredths,
    int32 expected_second_hundredths
) {
    LrcLyrics lyrics;
    LrcLyricsLine lyric_lines[2];
    LrcCtcLineTimestamps timestamps;
    LrcCtcLineTimestamp timestamp_lines[2];
    LrcOutputLine output_lines[3];
    LrcPipelineGenerateResult result;
    int32 output_line_count;
    int32 expected_output_line_count;
    int32 second_output_index;
    char first[] = "First";
    char second[] = "Second";

    lrc_lyrics_init(&lyrics);
    lrc_ctc_line_timestamps_init(&timestamps);
    lrc_pipeline_generate_result_init(&result);
    memset64(lyric_lines, 0, SIZEOF(lyric_lines));
    memset64(timestamp_lines, 0, SIZEOF(timestamp_lines));
    memset64(output_lines, 0, SIZEOF(output_lines));
    output_line_count = 0;

    lyrics.lines = lyric_lines;
    lyrics.line_count = LENGTH(lyric_lines);

    lyric_lines[0].text = first;
    lyric_lines[0].text_len = strlen32(first);
    lyric_lines[1].text = second;
    lyric_lines[1].text_len = strlen32(second);

    timestamps.lines = timestamp_lines;
    timestamps.line_count = LENGTH(timestamp_lines);
    timestamps.line_cap = LENGTH(timestamp_lines);
    timestamps.timestamped_line_count = LENGTH(timestamp_lines);

    timestamp_lines[0].line_index = 0;
    timestamp_lines[0].start_seconds = 1.0f;
    timestamp_lines[0].end_seconds = first_end_seconds;
    timestamp_lines[0].kind = LRC_CTC_LINE_TIMESTAMP_KIND_TIMESTAMPED;

    timestamp_lines[1].line_index = 1;
    timestamp_lines[1].start_seconds = second_start_seconds;
    timestamp_lines[1].end_seconds = second_start_seconds + 1.0f;
    timestamp_lines[1].kind = LRC_CTC_LINE_TIMESTAMP_KIND_TIMESTAMPED;

    if (!lrc_pipeline_output_lines_from_timestamps(&lyrics,
                                                   &timestamps,
                                                   output_lines,
                                                   LENGTH(output_lines),
                                                   &output_line_count,
                                                   &result)) {
        return pipeline_test_fail(name);
    }

    expected_output_line_count = 2;
    if (expect_clear) {
        expected_output_line_count = 3;
    }
    if (output_line_count != expected_output_line_count) {
        return pipeline_test_fail(name);
    }
    if ((output_lines[0].kind != LRC_OUTPUT_LINE_KIND_TIMESTAMPED)
        || (output_lines[0].timestamp_hundredths != 100)
        || !pipeline_test_output_text_equal(output_lines + 0,
                                            STRLIT("First"))) {
        return pipeline_test_fail(name);
    }

    second_output_index = 1;
    if (expect_clear) {
        if ((output_lines[1].kind != LRC_OUTPUT_LINE_KIND_TIMESTAMPED)
            || (output_lines[1].timestamp_hundredths
                != expected_clear_hundredths)
            || (output_lines[1].text_len != 0)) {
            return pipeline_test_fail(name);
        }
        second_output_index = 2;
    }

    if ((output_lines[second_output_index].kind
         != LRC_OUTPUT_LINE_KIND_TIMESTAMPED)
        || (output_lines[second_output_index].timestamp_hundredths
            != expected_second_hundredths)
        || !pipeline_test_output_text_equal(output_lines + second_output_index,
                                            STRLIT("Second"))) {
        return pipeline_test_fail(name);
    }

    return 0;
}

static int32
pipeline_test_line_timestamp_clear_gap_edges(void) {
    if (pipeline_test_line_timestamp_clear_case(
        "line clear gap below threshold",
        3.40f,
        4.39f,
        false,
        -1,
        439
    ) != 0) {
        return 1;
    }
    if (pipeline_test_line_timestamp_clear_case(
        "line clear gap at threshold",
        3.40f,
        4.40f,
        true,
        340,
        440
    ) != 0) {
        return 1;
    }
    if (pipeline_test_line_timestamp_clear_case(
        "line clear zero gap",
        3.40f,
        3.40f,
        false,
        -1,
        340
    ) != 0) {
        return 1;
    }
    if (pipeline_test_line_timestamp_clear_case(
        "line clear negative gap",
        3.40f,
        3.00f,
        false,
        -1,
        300
    ) != 0) {
        return 1;
    }
    if (pipeline_test_line_timestamp_clear_case(
        "line clear invalid current duration",
        1.00f,
        3.00f,
        false,
        -1,
        300
    ) != 0) {
        return 1;
    }

    return 0;
}

static int32
pipeline_test_line_timestamp_clear_keeps_blank_line(void) {
    LrcLyrics lyrics;
    LrcLyricsLine lyric_lines[3];
    LrcCtcLineTimestamps timestamps;
    LrcCtcLineTimestamp timestamp_lines[3];
    LrcOutputLine output_lines[4];
    LrcPipelineGenerateResult result;
    int32 output_line_count;
    char first[] = "First";
    char second[] = "Second";
    char blank[] = "";

    lrc_lyrics_init(&lyrics);
    lrc_ctc_line_timestamps_init(&timestamps);
    lrc_pipeline_generate_result_init(&result);
    memset64(lyric_lines, 0, SIZEOF(lyric_lines));
    memset64(timestamp_lines, 0, SIZEOF(timestamp_lines));
    memset64(output_lines, 0, SIZEOF(output_lines));
    output_line_count = 0;

    lyrics.lines = lyric_lines;
    lyrics.line_count = LENGTH(lyric_lines);

    lyric_lines[0].text = first;
    lyric_lines[0].text_len = strlen32(first);
    lyric_lines[1].text = blank;
    lyric_lines[1].text_len = 0;
    lyric_lines[2].text = second;
    lyric_lines[2].text_len = strlen32(second);

    timestamps.lines = timestamp_lines;
    timestamps.line_count = LENGTH(timestamp_lines);
    timestamps.line_cap = LENGTH(timestamp_lines);
    timestamps.timestamped_line_count = 2;
    timestamps.blank_line_count = 1;

    timestamp_lines[0].line_index = 0;
    timestamp_lines[0].start_seconds = 1.0f;
    timestamp_lines[0].end_seconds = 3.40f;
    timestamp_lines[0].kind = LRC_CTC_LINE_TIMESTAMP_KIND_TIMESTAMPED;

    timestamp_lines[1].line_index = 1;
    timestamp_lines[1].kind = LRC_CTC_LINE_TIMESTAMP_KIND_BLANK;

    timestamp_lines[2].line_index = 2;
    timestamp_lines[2].start_seconds = 7.58f;
    timestamp_lines[2].end_seconds = 9.0f;
    timestamp_lines[2].kind = LRC_CTC_LINE_TIMESTAMP_KIND_TIMESTAMPED;

    if (!lrc_pipeline_output_lines_from_timestamps(&lyrics,
                                                   &timestamps,
                                                   output_lines,
                                                   LENGTH(output_lines),
                                                   &output_line_count,
                                                   &result)) {
        return pipeline_test_fail("line timestamp blank conversion failed");
    }
    if (output_line_count != LENGTH(output_lines)) {
        return pipeline_test_fail("line timestamp blank output count");
    }
    if ((output_lines[0].kind != LRC_OUTPUT_LINE_KIND_TIMESTAMPED)
        || (output_lines[0].timestamp_hundredths != 100)
        || !pipeline_test_output_text_equal(output_lines + 0,
                                            STRLIT("First"))) {
        return pipeline_test_fail("blank case first line output");
    }
    if ((output_lines[1].kind != LRC_OUTPUT_LINE_KIND_TIMESTAMPED)
        || (output_lines[1].timestamp_hundredths != 340)
        || (output_lines[1].text_len != 0)) {
        return pipeline_test_fail("blank case clear line output");
    }
    if ((output_lines[2].kind != LRC_OUTPUT_LINE_KIND_BLANK)
        || (output_lines[2].timestamp_hundredths != -1)
        || (output_lines[2].text_len != 0)) {
        return pipeline_test_fail("blank case physical blank output");
    }
    if ((output_lines[3].kind != LRC_OUTPUT_LINE_KIND_TIMESTAMPED)
        || (output_lines[3].timestamp_hundredths != 758)
        || !pipeline_test_output_text_equal(output_lines + 3,
                                            STRLIT("Second"))) {
        return pipeline_test_fail("blank case second line output");
    }

    return 0;
}


static bool
pipeline_test_sample_has_blank_line_between(
    char *text,
    int32 text_len,
    char *first,
    int32 first_len,
    char *second,
    int32 second_len
) {
    bool found_first;
    bool saw_blank;
    int32 pos;

    found_first = false;
    saw_blank = false;
    pos = 0;

    while (pos < text_len) {
        int32 start;
        int32 end;
        int32 len;

        start = pos;
        while ((pos < text_len) && (text[pos] != '\n')) {
            pos += 1;
        }
        end = pos;
        if ((end > start) && (text[end - 1] == '\r')) {
            end -= 1;
        }
        while ((end > start)
               && ((text[end - 1] == ' ') || (text[end - 1] == '\t'))) {
            end -= 1;
        }
        while ((start < end)
               && ((text[start] == ' ') || (text[start] == '\t'))) {
            start += 1;
        }
        len = end - start;

        if (!found_first) {
            if (strequal2(text + start, len, first, first_len)) {
                found_first = true;
                saw_blank = false;
            }
        } else if (len == 0) {
            saw_blank = true;
        } else if (saw_blank
                   && strequal2(text + start, len, second, second_len)) {
            return true;
        } else {
            found_first = strequal2(text + start, len, first, first_len);
            saw_blank = false;
        }

        if (pos < text_len) {
            pos += 1;
        }
    }

    return false;
}

static int32
pipeline_test_line_timestamp_end_writes_clear_line(void) {
    LrcLyrics lyrics;
    LrcLyricsLine lyric_lines[2];
    LrcCtcLineTimestamps timestamps;
    LrcCtcLineTimestamp timestamp_lines[2];
    LrcOutputLine output_lines[3];
    LrcPipelineGenerateResult result;
    int32 output_line_count;
    char first[] = "First";
    char second[] = "Second";

    lrc_lyrics_init(&lyrics);
    lrc_ctc_line_timestamps_init(&timestamps);
    lrc_pipeline_generate_result_init(&result);
    memset64(lyric_lines, 0, SIZEOF(lyric_lines));
    memset64(timestamp_lines, 0, SIZEOF(timestamp_lines));
    memset64(output_lines, 0, SIZEOF(output_lines));
    output_line_count = 0;

    lyrics.lines = lyric_lines;
    lyrics.line_count = LENGTH(lyric_lines);

    lyric_lines[0].text = first;
    lyric_lines[0].text_len = strlen32(first);
    lyric_lines[1].text = second;
    lyric_lines[1].text_len = strlen32(second);

    timestamps.lines = timestamp_lines;
    timestamps.line_count = LENGTH(timestamp_lines);
    timestamps.line_cap = LENGTH(timestamp_lines);
    timestamps.timestamped_line_count = LENGTH(timestamp_lines);

    timestamp_lines[0].line_index = 0;
    timestamp_lines[0].start_seconds = 1.0f;
    timestamp_lines[0].end_seconds = 3.40f;
    timestamp_lines[0].kind = LRC_CTC_LINE_TIMESTAMP_KIND_TIMESTAMPED;

    timestamp_lines[1].line_index = 1;
    timestamp_lines[1].start_seconds = 7.58f;
    timestamp_lines[1].end_seconds = 9.0f;
    timestamp_lines[1].kind = LRC_CTC_LINE_TIMESTAMP_KIND_TIMESTAMPED;

    if (!lrc_pipeline_output_lines_from_timestamps(&lyrics,
                                                   &timestamps,
                                                   output_lines,
                                                   LENGTH(output_lines),
                                                   &output_line_count,
                                                   &result)) {
        return pipeline_test_fail("line timestamp conversion failed");
    }
    if (output_line_count != LENGTH(output_lines)) {
        return pipeline_test_fail("line timestamp output count");
    }
    if ((output_lines[0].kind != LRC_OUTPUT_LINE_KIND_TIMESTAMPED)
        || (output_lines[0].timestamp_hundredths != 100)
        || !pipeline_test_output_text_equal(output_lines + 0,
                                            STRLIT("First"))) {
        return pipeline_test_fail("first line start output");
    }
    if ((output_lines[1].kind != LRC_OUTPUT_LINE_KIND_TIMESTAMPED)
        || (output_lines[1].timestamp_hundredths != 340)
        || (output_lines[1].text_len != 0)) {
        return pipeline_test_fail("line end clear output");
    }
    if ((output_lines[2].kind != LRC_OUTPUT_LINE_KIND_TIMESTAMPED)
        || (output_lines[2].timestamp_hundredths != 758)
        || !pipeline_test_output_text_equal(output_lines + 2,
                                            STRLIT("Second"))) {
        return pipeline_test_fail("second line start output");
    }

    return 0;
}


static int32
pipeline_test_ctc_debug_dump_escape(void) {

    LrcCtcDebugDumpWriter writer;
    char temp_dir[PATH_MAX];
    char dump_path[PATH_MAX];
    char input[] = {
        'a',
        '\t',
        'b',
        '\n',
        'c',
        '\r',
        'd',
        '\\',
        'e',
        (char)1,
    };

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "ctc_dump_escape");
    pipeline_test_join_path(dump_path, SIZEOF(dump_path), temp_dir,
                            "dump.txt");

    if (!lrc_ctc_debug_dump_writer_open(&writer, dump_path)) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("open debug dump escape");
    }
    lrc_ctc_debug_dump_write_escaped_text(&writer,
                                          input,
                                          SIZEOF(input));
    if (!lrc_ctc_debug_dump_writer_close(&writer)) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("close debug dump escape");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("a\\tb\\nc\\rd\\\\e\\x01")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump escape contents");
    }

    test_remove_tree(temp_dir);

    return 0;
}

static int32
pipeline_test_ctc_debug_dump_text_and_tokens(void) {
    LrcPipelineConfig config;
    LrcPipeline pipeline;
    LrcLyricsNormalized normalized;
    LrcCtcDebugDumpWriter writer;
    LrcCtcTokenizedText tokenized;
    char temp_dir[PATH_MAX];
    char dump_path[PATH_MAX];
    LrcCtcToken tokenizer_tokens[] = {
        {.text = "", .text_len = 0, .id = 0, .is_blank = true},
        {.text = " ", .text_len = 1, .id = 1},
        {.text = "i", .text_len = 1, .id = 2},
        {.text = "ch", .text_len = 2, .id = 3},
    };
    LrcCtcTextToken text_tokens[] = {
        {
            .token_id = 2,
            .normalized_start = 0,
            .normalized_end = 1,
            .line_index = 0,
            .segment_index = 0,
            .starts_segment = true,
        },
        {
            .token_id = 3,
            .normalized_start = 1,
            .normalized_end = 3,
            .line_index = 0,
            .segment_index = 0,
            .starts_segment = false,
        },
    };
    LrcCtcTokenizer tokenizer = {
        .tokens = tokenizer_tokens,
        .token_count = LENGTH(tokenizer_tokens),
        .blank_id = 0,
        .unknown_id = -1,
    };

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "ctc_dump_tokens");
    pipeline_test_join_path(dump_path, SIZEOF(dump_path), temp_dir,
                            "dump.txt");

    lrc_pipeline_config_init(&config);
    config.lyrics_text_path = "lyrics.txt";
    config.output_lrc_path = "out.lrc";
    config.lyrics_preprocess_options.split_size =
        LRC_LYRICS_PREPROCESS_SPLIT_SIZE_WORD;
    config.lyrics_preprocess_options.romanization =
        LRC_LYRICS_PREPROCESS_ROMANIZATION_ICU;
    lrc_pipeline_init(&pipeline, &config);
    pipeline.ctc_assets.model_path = "model.onnx";
    pipeline.ctc_assets.tokenizer_path = "tokens.txt";
    pipeline.vocals_stage_path = "moskau-vocals.opus";

    lrc_lyrics_normalized_init(&normalized);
    normalized.text = "Ich\nMoskau";
    normalized.text_len = strlen32(normalized.text);
    normalized.target_text = "ich moskau";
    normalized.target_text_len = strlen32(normalized.target_text);

    lrc_ctc_tokenized_text_init(&tokenized);
    tokenized.tokens = text_tokens;
    tokenized.token_count = LENGTH(text_tokens);

    if (!lrc_ctc_debug_dump_writer_open(&writer, dump_path)) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("open debug dump tokens");
    }
    lrc_ctc_debug_dump_write_text_and_tokens(&writer,
                                             &pipeline,
                                             &normalized,
                                             &tokenizer,
                                             &tokenized);
    if (!lrc_ctc_debug_dump_writer_close(&writer)) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("close debug dump tokens");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("# lrc-ctc-parity-dump-v1\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump version");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("ctc_model_path=model.onnx\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump model path");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("split_size=word\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump split size");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("romanization=icu\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump romanization");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("[normalized_text]\ntext=Ich\\nMoskau\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump normalized text");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("[target_text]\ntext=ich moskau\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump target text");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("0\t2\ti\t0\t0\t1\t0\t1\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump first token");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("1\t3\tch\t0\t0\t0\t1\t3\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump second token");
    }

    test_remove_tree(temp_dir);

    return 0;
}

static int32
pipeline_test_ctc_debug_dump_audio_model(void) {
    LrcPipelineConfig config;
    LrcPipeline pipeline;
    LrcCtcDebugDumpWriter writer;
    LrcCtcAudio audio;
    LrcCtcModelInput input;
    LrcCtcEmissions emissions;
    LrcCtcTokenizer tokenizer;
    char temp_dir[PATH_MAX];
    char dump_path[PATH_MAX];

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "ctc_dump_frames");
    pipeline_test_join_path(dump_path, SIZEOF(dump_path), temp_dir,
                            "dump.txt");

    lrc_pipeline_config_init(&config);
    config.ctc_model_config.window_seconds = 42;
    config.ctc_model_config.context_seconds = 7;
    lrc_pipeline_init(&pipeline, &config);

    lrc_ctc_audio_init(&audio);
    audio.sample_rate = 16000;
    audio.sample_count = 123456;

    lrc_ctc_model_input_init(&input);
    input.sample_rate = 16000;
    input.inputs_to_logits_ratio = 320;
    input.stride_ms = 20.0;
    input.chunk_count = 3;
    input.row_sample_count = 512000;

    lrc_ctc_emissions_init(&emissions);
    emissions.frame_count = 6172;
    emissions.vocabulary_size = 1130;

    lrc_ctc_tokenizer_init(&tokenizer);
    tokenizer.token_count = 1129;
    tokenizer.blank_id = 0;

    if (!lrc_ctc_debug_dump_writer_open(&writer, dump_path)) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("open debug dump frames");
    }
    lrc_ctc_debug_dump_write_audio_model(&writer,
                                         &pipeline,
                                         &audio,
                                         &input,
                                         &emissions,
                                         &tokenizer,
                                         1130);
    if (!lrc_ctc_debug_dump_writer_close(&writer)) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("close debug dump frames");
    }
    if (!pipeline_test_file_contains(dump_path, STRLIT("[frames]\n"))) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump frames section");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("audio_sample_rate=16000\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump audio sample rate");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("audio_sample_count=123456\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump audio sample count");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("stride_ms=20\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump stride ms");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("frame_duration_seconds=0.02\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump frame duration");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("emission_frame_count=6172\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump emission frame count");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("emission_vocabulary_size=1130\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump vocabulary size");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("tokenizer_token_count=1129\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump tokenizer token count");
    }
    if (!pipeline_test_file_contains(dump_path, STRLIT("blank_token_id=0\n"))) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump blank token id");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("star_token_id=1130\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump star token id");
    }
    if (!pipeline_test_file_contains(dump_path, STRLIT("chunk_count=3\n"))) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump chunk count");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("row_sample_count=512000\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump row sample count");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("window_seconds=42\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump window seconds");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("context_seconds=7\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump context seconds");
    }

    test_remove_tree(temp_dir);

    return 0;
}

static int32
pipeline_test_ctc_debug_dump_path_segments(void) {
    LrcCtcDebugDumpWriter writer;
    LrcCtcPathSegments segments;
    char temp_dir[PATH_MAX];
    char dump_path[PATH_MAX];
    LrcCtcToken tokenizer_tokens[] = {
        {.text = "", .text_len = 0, .id = 0, .is_blank = true},
        {.text = "A", .text_len = 1, .id = 1},
        {.text = "B", .text_len = 1, .id = 2},
    };
    LrcCtcTokenizer tokenizer = {
        .tokens = tokenizer_tokens,
        .token_count = LENGTH(tokenizer_tokens),
        .blank_id = 0,
        .unknown_id = -1,
    };
    LrcCtcPathSegment path_segments[] = {
        {
            .token_index = -1,
            .start_frame = 0,
            .end_frame = 2,
            .start_seconds = 0.0f,
            .end_seconds = 2.0f,
            .score = -1.0f,
            .token_id = 0,
            .is_blank = true,
        },
        {
            .token_index = 0,
            .start_frame = 2,
            .end_frame = 4,
            .start_seconds = 2.0f,
            .end_seconds = 4.0f,
            .score = -2.0f,
            .token_id = 1,
        },
        {
            .token_index = -1,
            .start_frame = 4,
            .end_frame = 5,
            .start_seconds = 4.0f,
            .end_seconds = 5.0f,
            .score = -2.0f,
            .token_id = 0,
            .is_blank = true,
        },
        {
            .token_index = 1,
            .start_frame = 5,
            .end_frame = 6,
            .start_seconds = 5.0f,
            .end_seconds = 6.0f,
            .score = -3.0f,
            .token_id = 2,
        },
        {
            .token_index = -1,
            .start_frame = 6,
            .end_frame = 7,
            .start_seconds = 6.0f,
            .end_seconds = 7.0f,
            .score = -4.0f,
            .token_id = 0,
            .is_blank = true,
        },
    };

    test_make_temp_dir(temp_dir,
                       SIZEOF(temp_dir),
                       "ctc_dump_path_segments");
    pipeline_test_join_path(dump_path, SIZEOF(dump_path), temp_dir,
                            "dump.txt");

    segments.segments = path_segments;
    segments.segment_count = LENGTH(path_segments);
    segments.segment_cap = LENGTH(path_segments);

    if (!lrc_ctc_debug_dump_writer_open(&writer, dump_path)) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("open debug dump path segments");
    }
    lrc_ctc_debug_dump_write_path_segments(&writer, &tokenizer, &segments);
    if (!lrc_ctc_debug_dump_writer_close(&writer)) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("close debug dump path segments");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("[merged_path_segments]\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump path segment section");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("index\ttoken_index\ttoken_id\ttoken_text")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump path segment header");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("0\t-1\t0\t\t0\t2\t0\t2\t-1\t1\t0\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump first path segment");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("1\t0\t1\tA\t2\t4\t2\t4\t-2\t0\t0\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump second path segment");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("4\t-1\t0\t\t6\t7\t6\t7\t-4\t1\t0\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump fifth path segment");
    }

    test_remove_tree(temp_dir);

    return 0;
}


static int32
pipeline_test_ctc_debug_dump_word_spans(void) {
    LrcCtcDebugDumpWriter writer;
    LrcLyricsNormalized normalized;
    LrcCtcWordSpans before_spans;
    LrcCtcWordSpans after_spans;
    char temp_dir[PATH_MAX];
    char dump_path[PATH_MAX];
    char normalized_text[] = "Ich sehe";
    LrcCtcWordSpan before[] = {
        {
            .word_index = 0,
            .token_start_index = 0,
            .token_end_index = 3,
            .span_start_index = 0,
            .span_end_index = 3,
            .normalized_start = 0,
            .normalized_end = 3,
            .line_index = 0,
            .start_seconds = 1.0f,
            .end_seconds = 1.5f,
            .score = -1.0f,
        },
        {
            .word_index = 1,
            .token_start_index = 3,
            .token_end_index = 7,
            .span_start_index = 3,
            .span_end_index = 7,
            .normalized_start = 4,
            .normalized_end = 8,
            .line_index = 0,
            .start_seconds = 2.0f,
            .end_seconds = 2.25f,
            .score = -2.0f,
        },
    };
    LrcCtcWordSpan after[] = {
        {
            .word_index = 0,
            .token_start_index = 0,
            .token_end_index = 3,
            .span_start_index = 0,
            .span_end_index = 3,
            .normalized_start = 0,
            .normalized_end = 3,
            .line_index = 0,
            .start_seconds = 0.5f,
            .end_seconds = 1.75f,
            .score = -1.0f,
        },
        {
            .word_index = 1,
            .token_start_index = 3,
            .token_end_index = 7,
            .span_start_index = 3,
            .span_end_index = 7,
            .normalized_start = 4,
            .normalized_end = 8,
            .line_index = 0,
            .start_seconds = 1.75f,
            .end_seconds = 3.0f,
            .score = -2.0f,
        },
    };

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "ctc_dump_word_spans");
    pipeline_test_join_path(dump_path, SIZEOF(dump_path), temp_dir,
                            "dump.txt");

    lrc_lyrics_normalized_init(&normalized);
    normalized.text = normalized_text;
    normalized.text_len = strlen32(normalized.text);
    before_spans.spans = before;
    before_spans.span_count = LENGTH(before);
    before_spans.span_cap = LENGTH(before);
    after_spans.spans = after;
    after_spans.span_count = LENGTH(after);
    after_spans.span_cap = LENGTH(after);

    if (!lrc_ctc_debug_dump_writer_open(&writer, dump_path)) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("open debug dump word spans");
    }
    lrc_ctc_debug_dump_write_word_spans(&writer,
                                        "word_spans_before_padding",
                                        &normalized,
                                        &before_spans);
    lrc_ctc_debug_dump_write_word_spans(&writer,
                                        "word_spans_after_padding",
                                        &normalized,
                                        &after_spans);
    if (!lrc_ctc_debug_dump_writer_close(&writer)) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("close debug dump word spans");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("[word_spans_before_padding]\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump before word span section");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("[word_spans_after_padding]\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump after word span section");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("index\tline_index\tword_index\ttext\tnormalized_start")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump word span header");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("0\t0\t0\tIch\t0\t3\t0\t3\t0\t3\t1\t1.5\t-1\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump before first word span");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("0\t0\t0\tIch\t0\t3\t0\t3\t0\t3\t0.5\t1.75\t-1\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump after first word span");
    }
    if (!pipeline_test_file_contains(
        dump_path,
        STRLIT("1\t0\t1\tsehe\t4\t8\t3\t7\t3\t7\t1.75\t3\t-2\n")
    )) {
        test_remove_tree(temp_dir);
        return pipeline_test_fail("debug dump after second word span");
    }

    test_remove_tree(temp_dir);

    return 0;
}

static int32
pipeline_test_preprocess_option_parsers(void) {
    LrcPipelineConfig config;

    lrc_pipeline_config_init(&config);

    ASSERT(lrc_pipeline_parse_preprocess_split_size(&config, "word"));
    ASSERT(config.lyrics_preprocess_options.split_size
           == LRC_LYRICS_PREPROCESS_SPLIT_SIZE_WORD);
    ASSERT(lrc_pipeline_parse_preprocess_split_size(&config, "char"));
    ASSERT(config.lyrics_preprocess_options.split_size
           == LRC_LYRICS_PREPROCESS_SPLIT_SIZE_CHAR);
    ASSERT(lrc_pipeline_parse_preprocess_split_size(&config, "current"));
    ASSERT(config.lyrics_preprocess_options.split_size
           == LRC_LYRICS_PREPROCESS_SPLIT_SIZE_CURRENT);

    ASSERT(lrc_pipeline_parse_preprocess_star_frequency(&config, "none"));
    ASSERT(config.lyrics_preprocess_options.star_frequency
           == LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_NONE);
    ASSERT(lrc_pipeline_parse_preprocess_star_frequency(&config, "segment"));
    ASSERT(config.lyrics_preprocess_options.star_frequency
           == LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_SEGMENT);
    ASSERT(lrc_pipeline_parse_preprocess_star_frequency(&config, "edges"));
    ASSERT(config.lyrics_preprocess_options.star_frequency
           == LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_EDGES);

    ASSERT(lrc_pipeline_parse_preprocess_romanization(&config, "icu"));
    ASSERT(config.lyrics_preprocess_options.romanization
           == LRC_LYRICS_PREPROCESS_ROMANIZATION_ICU);
    ASSERT(lrc_pipeline_parse_preprocess_romanization(&config, "off"));
    ASSERT(config.lyrics_preprocess_options.romanization
           == LRC_LYRICS_PREPROCESS_ROMANIZATION_OFF);
    lrc_pipeline_enable_preprocess_romanization(&config);
    ASSERT(config.lyrics_preprocess_options.romanization
           == LRC_LYRICS_PREPROCESS_ROMANIZATION_ICU);

    ASSERT(lrc_pipeline_parse_preprocess_language(&config, "rus"));
    ASSERT(strequal2(config.lyrics_preprocess_options.language,
                     config.lyrics_preprocess_options.language_len,
                     STRLIT("rus")));

    return 0;
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
    ASSERT(config.ctc_debug_dump_path == NULL);
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
    ASSERT(config.lyrics_preprocess_options.split_size
           == LRC_LYRICS_PREPROCESS_SPLIT_SIZE_WORD);
    ASSERT(config.lyrics_preprocess_options.star_frequency
           == LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_EDGES);
    ASSERT(config.lyrics_preprocess_options.romanization
           == LRC_LYRICS_PREPROCESS_ROMANIZATION_ICU);
    ASSERT(strequal2(config.lyrics_preprocess_options.language,
                     config.lyrics_preprocess_options.language_len,
                     STRLIT("eng")));
    ASSERT(config.ctc_emission_values_kind
           == LRC_CTC_EMISSION_VALUES_LOGITS);
    ASSERT(pipeline.error == LRC_PIPELINE_ERROR_NONE);
    ASSERT(strequal(pipeline.message, "ok"));
    ASSERT(pipeline.path == NULL);
    ASSERT(!pipeline.prepared);
    ASSERT(!pipeline.owns_temp_dir);
    ASSERT(!pipeline.owns_vocals_path);
    ASSERT(!lrc_pipeline_debug_dump_enabled(&pipeline));

    pipeline.config.ctc_debug_dump_path = "dump.txt";
    ASSERT(lrc_pipeline_debug_dump_enabled(&pipeline));
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
    config.ort_session_config.execution_provider = ORT_EXECUTION_PROVIDER_CPU;
    config.ort_session_config.device_id = 2;

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
    ASSERT(request.ort_session_config.execution_provider
           == ORT_EXECUTION_PROVIDER_CPU);
    ASSERT(request.ort_session_config.device_id == 2);
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
    if (pipeline_test_line_timing_audio_available() != 0) {
        exit(1);
    }
    if (pipeline_test_line_timestamp_end_writes_clear_line() != 0) {
        exit(1);
    }
    if (pipeline_test_line_timestamp_clear_gap_edges() != 0) {
        exit(1);
    }
    if (pipeline_test_line_timestamp_clear_keeps_blank_line() != 0) {
        exit(1);
    }
    if (pipeline_test_preprocess_option_parsers() != 0) {
        exit(1);
    }
    if (pipeline_test_ctc_debug_dump_escape() != 0) {
        exit(1);
    }
    if (pipeline_test_ctc_debug_dump_text_and_tokens() != 0) {
        exit(1);
    }
    if (pipeline_test_ctc_debug_dump_audio_model() != 0) {
        exit(1);
    }
    if (pipeline_test_ctc_debug_dump_path_segments() != 0) {
        exit(1);
    }
    if (pipeline_test_ctc_debug_dump_word_spans() != 0) {
        exit(1);
    }
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
