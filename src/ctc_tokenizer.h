#if !defined(CTC_TOKENIZER_H)
#define CTC_TOKENIZER_H

#include "cbase.h"

enum LrcCtcTokenizerError {
    LRC_CTC_TOKENIZER_ERROR_NONE,
    LRC_CTC_TOKENIZER_ERROR_INVALID_ARGUMENT,
    LRC_CTC_TOKENIZER_ERROR_MISSING_PATH,
    LRC_CTC_TOKENIZER_ERROR_OPEN_FAILED,
    LRC_CTC_TOKENIZER_ERROR_READ_FAILED,
    LRC_CTC_TOKENIZER_ERROR_FILE_TOO_LARGE,
    LRC_CTC_TOKENIZER_ERROR_EMPTY,
    LRC_CTC_TOKENIZER_ERROR_INVALID_UTF8,
    LRC_CTC_TOKENIZER_ERROR_EMPTY_TOKEN,
    LRC_CTC_TOKENIZER_ERROR_DUPLICATE_TOKEN,
    LRC_CTC_TOKENIZER_ERROR_MISSING_BLANK,
    LRC_CTC_TOKENIZER_ERROR_TOO_MANY_TOKENS,
};

typedef struct LrcCtcTokenizerResult {
    enum LrcCtcTokenizerError error;
    char *message;
    char *path;

    int32 line_index;
    int32 token_id;
} LrcCtcTokenizerResult;

typedef struct LrcCtcToken {
    char *text;

    int32 text_len;
    int32 id;

    bool is_blank;
    bool is_unknown;
} LrcCtcToken;

typedef struct LrcCtcTokenizer {
    LrcCtcToken *tokens;
    char *text_storage;

    int32 token_count;
    int32 token_cap;
    int32 text_storage_len;
    int32 text_storage_cap;
    int32 blank_id;
    int32 unknown_id;
} LrcCtcTokenizer;

static void lrc_ctc_tokenizer_init(LrcCtcTokenizer *tokenizer);
static void lrc_ctc_tokenizer_destroy(LrcCtcTokenizer *tokenizer);
static void lrc_ctc_tokenizer_result_init(LrcCtcTokenizerResult *result);
static bool lrc_ctc_tokenizer_load_file(
    LrcCtcTokenizer *tokenizer,
    char *path,
    LrcCtcTokenizerResult *result
);
static LrcCtcToken *lrc_ctc_tokenizer_id_to_token(
    LrcCtcTokenizer *tokenizer,
    int32 id
);
static bool lrc_ctc_tokenizer_token_id(
    LrcCtcTokenizer *tokenizer,
    char *token,
    int32 token_len,
    int32 *id
);

#endif /* CTC_TOKENIZER_H */
