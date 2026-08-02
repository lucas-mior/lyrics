#include "lyricsync.h"
#include "cbase.h"
#include "ctc_assets.h"

#if !defined(TESTING_ctc_assets)
#define TESTING_ctc_assets 0
#endif

static void
lrc_ctc_assets_config_init(LrcCtcAssetsConfig *config) {
    if (config == NULL) {
        return;
    }

    memset64(config, 0, SIZEOF(*config));

    return;
}

static void
lrc_ctc_assets_result_init(LrcCtcAssetsResult *result) {
    if (result == NULL) {
        return;
    }

    result->error = LRC_CTC_ASSETS_ERROR_NONE;
    result->message = "ok";
    result->path = NULL;

    return;
}

static void
lrc_ctc_assets_init(LrcCtcAssets *assets) {
    if (assets == NULL) {
        return;
    }

    memset64(assets, 0, SIZEOF(*assets));

    return;
}

static void
lrc_ctc_assets_result_set(
    LrcCtcAssetsResult *result,
    enum LrcCtcAssetsError error,
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
lrc_ctc_assets_path_missing(char *path) {
    if (path == NULL) {
        return true;
    }
    if (path[0] == '\0') {
        return true;
    }

    return false;
}

static bool
lrc_ctc_assets_validate(
    LrcCtcAssets *assets,
    LrcCtcAssetsConfig *config,
    LrcCtcAssetsResult *result
) {
    if ((assets == NULL) || (config == NULL)) {
        lrc_ctc_assets_result_set(
            result,
            LRC_CTC_ASSETS_ERROR_INVALID_ARGUMENT,
            "CTC asset validation received invalid arguments",
            NULL
        );
        return false;
    }

    lrc_ctc_assets_init(assets);
    lrc_ctc_assets_result_init(result);

    if (lrc_ctc_assets_path_missing(config->model_path)) {
        lrc_ctc_assets_result_set(
            result,
            LRC_CTC_ASSETS_ERROR_MISSING_MODEL_PATH,
            "CTC model path is missing",
            config->model_path
        );
        return false;
    }
    if (lrc_ctc_assets_path_missing(config->tokenizer_path)) {
        lrc_ctc_assets_result_set(
            result,
            LRC_CTC_ASSETS_ERROR_MISSING_TOKENIZER_PATH,
            "CTC tokenizer path is missing",
            config->tokenizer_path
        );
        return false;
    }
    if (!util_file_exists(config->model_path)) {
        lrc_ctc_assets_result_set(
            result,
            LRC_CTC_ASSETS_ERROR_MODEL_NOT_FOUND,
            "CTC model file was not found",
            config->model_path
        );
        return false;
    }
    if (!util_file_exists(config->tokenizer_path)) {
        lrc_ctc_assets_result_set(
            result,
            LRC_CTC_ASSETS_ERROR_TOKENIZER_NOT_FOUND,
            "CTC tokenizer file was not found",
            config->tokenizer_path
        );
        return false;
    }

    assets->model_path = config->model_path;
    assets->tokenizer_path = config->tokenizer_path;
    assets->validated = true;

    return true;
}

#if TESTING_ctc_assets

#define CBASE_IMPLEMENT
#include "cbase.h"

static int32
ctc_assets_test_fail(char *name) {
    error2("CTC assets test failed: %s\n", name);

    return 1;
}

static bool
ctc_assets_write_file(char *path, char *text) {
    return write_entire_file(path, text, strlen32(text));
}

static void
ctc_assets_join_path(
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
ctc_assets_test_config_defaults(void) {
    LrcCtcAssetsConfig config;
    LrcCtcAssetsResult result;
    LrcCtcAssets assets;

    lrc_ctc_assets_config_init(&config);
    lrc_ctc_assets_result_init(&result);
    lrc_ctc_assets_init(&assets);

    ASSERT(config.model_path == NULL);
    ASSERT(config.tokenizer_path == NULL);
    ASSERT(result.error == LRC_CTC_ASSETS_ERROR_NONE);
    ASSERT(strequal(result.message, "ok"));
    ASSERT(result.path == NULL);
    ASSERT(assets.model_path == NULL);
    ASSERT(assets.tokenizer_path == NULL);
    ASSERT(!assets.validated);

    return 0;
}

static int32
ctc_assets_test_valid_generated_files(void) {
    LrcCtcAssetsConfig config;
    LrcCtcAssetsResult result;
    LrcCtcAssets assets;
    char temp_dir[PATH_MAX];
    char model_path[PATH_MAX];
    char tokenizer_path[PATH_MAX];

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "ctc_assets_valid");
    ctc_assets_join_path(model_path, SIZEOF(model_path), temp_dir,
                         "model.onnx");
    ctc_assets_join_path(tokenizer_path, SIZEOF(tokenizer_path), temp_dir,
                         "tokens.txt");

    if (!ctc_assets_write_file(model_path, "fake model\n")) {
        test_remove_tree(temp_dir);
        return ctc_assets_test_fail("write fake model");
    }
    if (!ctc_assets_write_file(tokenizer_path, "<blank>\na\nb\n")) {
        test_remove_tree(temp_dir);
        return ctc_assets_test_fail("write fake tokenizer");
    }

    lrc_ctc_assets_config_init(&config);
    config.model_path = model_path;
    config.tokenizer_path = tokenizer_path;

    if (!lrc_ctc_assets_validate(&assets, &config, &result)) {
        test_remove_tree(temp_dir);
        return ctc_assets_test_fail("validate generated files");
    }
    ASSERT(result.error == LRC_CTC_ASSETS_ERROR_NONE);
    ASSERT(strequal(result.message, "ok"));
    ASSERT(strequal(assets.model_path, model_path));
    ASSERT(strequal(assets.tokenizer_path, tokenizer_path));
    ASSERT(assets.validated);

    test_remove_tree(temp_dir);

    return 0;
}

static int32
ctc_assets_test_missing_model_path(void) {
    LrcCtcAssetsConfig config;
    LrcCtcAssetsResult result;
    LrcCtcAssets assets;

    lrc_ctc_assets_config_init(&config);
    config.tokenizer_path = "tokens.txt";

    if (lrc_ctc_assets_validate(&assets, &config, &result)) {
        return ctc_assets_test_fail("missing model path accepted");
    }
    ASSERT(result.error == LRC_CTC_ASSETS_ERROR_MISSING_MODEL_PATH);
    ASSERT(strequal(result.message, "CTC model path is missing"));
    ASSERT(result.path == NULL);
    ASSERT(!assets.validated);

    return 0;
}

static int32
ctc_assets_test_missing_tokenizer_path(void) {
    LrcCtcAssetsConfig config;
    LrcCtcAssetsResult result;
    LrcCtcAssets assets;

    lrc_ctc_assets_config_init(&config);
    config.model_path = "model.onnx";

    if (lrc_ctc_assets_validate(&assets, &config, &result)) {
        return ctc_assets_test_fail("missing tokenizer path accepted");
    }
    ASSERT(result.error == LRC_CTC_ASSETS_ERROR_MISSING_TOKENIZER_PATH);
    ASSERT(strequal(result.message, "CTC tokenizer path is missing"));
    ASSERT(result.path == NULL);
    ASSERT(!assets.validated);

    return 0;
}

static int32
ctc_assets_test_missing_model_file(void) {
    LrcCtcAssetsConfig config;
    LrcCtcAssetsResult result;
    LrcCtcAssets assets;
    char temp_dir[PATH_MAX];
    char model_path[PATH_MAX];
    char tokenizer_path[PATH_MAX];

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "ctc_assets_no_model");
    ctc_assets_join_path(model_path, SIZEOF(model_path), temp_dir,
                         "missing.onnx");
    ctc_assets_join_path(tokenizer_path, SIZEOF(tokenizer_path), temp_dir,
                         "tokens.txt");

    if (!ctc_assets_write_file(tokenizer_path, "<blank>\na\n")) {
        test_remove_tree(temp_dir);
        return ctc_assets_test_fail("write tokenizer for missing model test");
    }

    lrc_ctc_assets_config_init(&config);
    config.model_path = model_path;
    config.tokenizer_path = tokenizer_path;

    if (lrc_ctc_assets_validate(&assets, &config, &result)) {
        test_remove_tree(temp_dir);
        return ctc_assets_test_fail("missing model file accepted");
    }
    ASSERT(result.error == LRC_CTC_ASSETS_ERROR_MODEL_NOT_FOUND);
    ASSERT(strequal(result.path, model_path));
    ASSERT(!assets.validated);

    test_remove_tree(temp_dir);

    return 0;
}

static int32
ctc_assets_test_missing_tokenizer_file(void) {
    LrcCtcAssetsConfig config;
    LrcCtcAssetsResult result;
    LrcCtcAssets assets;
    char temp_dir[PATH_MAX];
    char model_path[PATH_MAX];
    char tokenizer_path[PATH_MAX];

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "ctc_assets_no_tokens");
    ctc_assets_join_path(model_path, SIZEOF(model_path), temp_dir,
                         "model.onnx");
    ctc_assets_join_path(tokenizer_path, SIZEOF(tokenizer_path), temp_dir,
                         "missing.txt");

    if (!ctc_assets_write_file(model_path, "fake model\n")) {
        test_remove_tree(temp_dir);
        return ctc_assets_test_fail("write model for missing tokenizer test");
    }

    lrc_ctc_assets_config_init(&config);
    config.model_path = model_path;
    config.tokenizer_path = tokenizer_path;

    if (lrc_ctc_assets_validate(&assets, &config, &result)) {
        test_remove_tree(temp_dir);
        return ctc_assets_test_fail("missing tokenizer file accepted");
    }
    ASSERT(result.error == LRC_CTC_ASSETS_ERROR_TOKENIZER_NOT_FOUND);
    ASSERT(strequal(result.path, tokenizer_path));
    ASSERT(!assets.validated);

    test_remove_tree(temp_dir);

    return 0;
}

static int32
ctc_assets_test_invalid_arguments(void) {
    LrcCtcAssetsConfig config;
    LrcCtcAssetsResult result;
    LrcCtcAssets assets;

    lrc_ctc_assets_config_init(&config);

    if (lrc_ctc_assets_validate(NULL, &config, &result)) {
        return ctc_assets_test_fail("null assets accepted");
    }
    ASSERT(result.error == LRC_CTC_ASSETS_ERROR_INVALID_ARGUMENT);

    if (lrc_ctc_assets_validate(&assets, NULL, &result)) {
        return ctc_assets_test_fail("null config accepted");
    }
    ASSERT(result.error == LRC_CTC_ASSETS_ERROR_INVALID_ARGUMENT);

    return 0;
}

int32
main(void) {
    if (ctc_assets_test_config_defaults() != 0) {
        exit(1);
    }
    if (ctc_assets_test_valid_generated_files() != 0) {
        exit(1);
    }
    if (ctc_assets_test_missing_model_path() != 0) {
        exit(1);
    }
    if (ctc_assets_test_missing_tokenizer_path() != 0) {
        exit(1);
    }
    if (ctc_assets_test_missing_model_file() != 0) {
        exit(1);
    }
    if (ctc_assets_test_missing_tokenizer_file() != 0) {
        exit(1);
    }
    if (ctc_assets_test_invalid_arguments() != 0) {
        exit(1);
    }

    return 0;
}

#endif /* TESTING_ctc_assets */
