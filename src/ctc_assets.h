#if !defined(CTC_ASSETS_H)
#define CTC_ASSETS_H

#include "cbase.h"

enum LrcCtcAssetsError {
    LRC_CTC_ASSETS_ERROR_NONE,
    LRC_CTC_ASSETS_ERROR_INVALID_ARGUMENT,
    LRC_CTC_ASSETS_ERROR_MISSING_MODEL_PATH,
    LRC_CTC_ASSETS_ERROR_MISSING_TOKENIZER_PATH,
    LRC_CTC_ASSETS_ERROR_MODEL_NOT_FOUND,
    LRC_CTC_ASSETS_ERROR_TOKENIZER_NOT_FOUND,
};

typedef struct LrcCtcAssetsConfig {
    char *model_path;
    char *tokenizer_path;
} LrcCtcAssetsConfig;

typedef struct LrcCtcAssetsResult {
    enum LrcCtcAssetsError error;
    char *message;
    char *path;
} LrcCtcAssetsResult;

typedef struct LrcCtcAssets {
    char *model_path;
    char *tokenizer_path;

    bool validated;
} LrcCtcAssets;

static void lrc_ctc_assets_result_init(LrcCtcAssetsResult *result);
static bool lrc_ctc_assets_validate(
    LrcCtcAssets *assets,
    LrcCtcAssetsConfig *config,
    LrcCtcAssetsResult *result
);

#endif /* CTC_ASSETS_H */
