#include "ctc_tokenizer.h"

#include "cbase.h"

#if !defined(TESTING_ctc_tokenizer)
#define TESTING_ctc_tokenizer 0
#endif

static void
lrc_ctc_tokenizer_init(LrcCtcTokenizer *tokenizer) {
    if (tokenizer == NULL) {
        return;
    }

    memset64(tokenizer, 0, SIZEOF(*tokenizer));
    tokenizer->blank_id = -1;
    tokenizer->unknown_id = -1;

    return;
}

static void
lrc_ctc_tokenizer_destroy(LrcCtcTokenizer *tokenizer) {
    if (tokenizer == NULL) {
        return;
    }

    if (tokenizer->tokens) {
        free2(tokenizer->tokens,
              tokenizer->token_cap*SIZEOF(*tokenizer->tokens));
    }
    if (tokenizer->text_storage) {
        free2(tokenizer->text_storage,
              tokenizer->text_storage_cap*SIZEOF(*tokenizer->text_storage));
    }

    lrc_ctc_tokenizer_init(tokenizer);

    return;
}

static void
lrc_ctc_tokenizer_result_init(LrcCtcTokenizerResult *result) {
    if (result == NULL) {
        return;
    }

    result->error = LRC_CTC_TOKENIZER_ERROR_NONE;
    result->message = "ok";
    result->path = NULL;

    result->line_index = -1;
    result->token_id = -1;

    return;
}

static void
lrc_ctc_tokenizer_result_set(
    LrcCtcTokenizerResult *result,
    enum LrcCtcTokenizerError error,
    char *message,
    char *path,
    int32 line_index,
    int32 token_id
) {
    if (result == NULL) {
        return;
    }

    result->error = error;
    result->message = message;
    result->path = path;

    result->line_index = line_index;
    result->token_id = token_id;

    return;
}

static bool
lrc_ctc_tokenizer_path_missing(char *path) {
    if (path == NULL) {
        return true;
    }
    if (path[0] == '\0') {
        return true;
    }

    return false;
}

static bool
lrc_ctc_tokenizer_read_file(
    char *path,
    char **file_text,
    int32 *file_len,
    LrcCtcTokenizerResult *result
) {
    FILE *file;
    int64 len;
    int64 read_len;
    char *text;

    if ((file = fopen(path, "rb")) == NULL) {
        lrc_ctc_tokenizer_result_set(
            result,
            LRC_CTC_TOKENIZER_ERROR_OPEN_FAILED,
            "could not open CTC tokenizer file",
            path,
            -1,
            -1
        );
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        lrc_ctc_tokenizer_result_set(
            result,
            LRC_CTC_TOKENIZER_ERROR_READ_FAILED,
            "could not seek CTC tokenizer file",
            path,
            -1,
            -1
        );
        XFCLOSE(file, path);
        return false;
    }
    if ((len = ftell(file)) < 0) {
        lrc_ctc_tokenizer_result_set(
            result,
            LRC_CTC_TOKENIZER_ERROR_READ_FAILED,
            "could not measure CTC tokenizer file",
            path,
            -1,
            -1
        );
        XFCLOSE(file, path);
        return false;
    }
    if (len >= MAXOF(*file_len)) {
        lrc_ctc_tokenizer_result_set(
            result,
            LRC_CTC_TOKENIZER_ERROR_FILE_TOO_LARGE,
            "CTC tokenizer file is too large",
            path,
            -1,
            -1
        );
        XFCLOSE(file, path);
        return false;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        lrc_ctc_tokenizer_result_set(
            result,
            LRC_CTC_TOKENIZER_ERROR_READ_FAILED,
            "could not rewind CTC tokenizer file",
            path,
            -1,
            -1
        );
        XFCLOSE(file, path);
        return false;
    }

    text = malloc2(len + 1);
    read_len = 0;
    if (len > 0) {
        read_len = fread64(text, 1, len, file);
    }
    if (read_len != len) {
        lrc_ctc_tokenizer_result_set(
            result,
            LRC_CTC_TOKENIZER_ERROR_READ_FAILED,
            "could not read CTC tokenizer file",
            path,
            -1,
            -1
        );
        free2(text, (len + 1)*SIZEOF(*text));
        XFCLOSE(file, path);
        return false;
    }
    text[read_len] = '\0';
    XFCLOSE(file, path);

    *file_text = text;
    *file_len = (int32)read_len;

    return true;
}

static bool
lrc_ctc_tokenizer_utf8_valid(char *text, int32 text_len, int32 *bad_offset) {
    uint32 rune;

    for (int32 i = 0; i < text_len;) {
        char encoded[4];
        int32 step;
        int32 encoded_len;

        step = utf8_decode_raw(text + i, &rune, text_len - i);
        encoded_len = utf8_encode_raw(rune, encoded);
        if ((step <= 0)
            || (encoded_len != step)
            || (memcmp64(encoded, text + i, step) != 0)) {
            if (bad_offset) {
                *bad_offset = i;
            }
            return false;
        }
        i += step;
    }

    return true;
}

static bool
lrc_ctc_tokenizer_reserve_tokens(
    LrcCtcTokenizer *tokenizer,
    int32 extra
) {
    int64 needed;
    int32 new_cap;

    if (extra <= 0) {
        return true;
    }

    needed = (int64)tokenizer->token_count + extra;
    if (needed <= tokenizer->token_cap) {
        return true;
    }
    if (needed >= MAXOF(tokenizer->token_cap)) {
        return false;
    }

    new_cap = tokenizer->token_cap;
    if (new_cap <= 0) {
        new_cap = 16;
    }
    while (new_cap < needed) {
        if (new_cap >= (MAXOF(new_cap)/2)) {
            new_cap = (int32)needed;
            break;
        }
        new_cap *= 2;
    }

    tokenizer->tokens = realloc2(tokenizer->tokens,
                                  tokenizer->token_cap,
                                  new_cap,
                                  SIZEOF(*tokenizer->tokens));
    tokenizer->token_cap = new_cap;

    return true;
}

static bool
lrc_ctc_tokenizer_reserve_text(
    LrcCtcTokenizer *tokenizer,
    int32 extra
) {
    int64 needed;
    int32 new_cap;

    if (extra <= 0) {
        return true;
    }

    needed = (int64)tokenizer->text_storage_len + extra;
    if (needed <= tokenizer->text_storage_cap) {
        return true;
    }
    if (needed >= MAXOF(tokenizer->text_storage_cap)) {
        return false;
    }

    new_cap = tokenizer->text_storage_cap;
    if (new_cap <= 0) {
        new_cap = 128;
    }
    while (new_cap < needed) {
        if (new_cap >= (MAXOF(new_cap)/2)) {
            new_cap = (int32)needed;
            break;
        }
        new_cap *= 2;
    }

    tokenizer->text_storage = realloc2(tokenizer->text_storage,
                                        tokenizer->text_storage_cap,
                                        new_cap,
                                        SIZEOF(*tokenizer->text_storage));
    tokenizer->text_storage_cap = new_cap;

    return true;
}

static bool
lrc_ctc_tokenizer_decode_marker(
    char *line,
    int32 line_len,
    char **token_text,
    int32 *token_len,
    bool *is_blank,
    bool *is_unknown
) {
    *token_text = line;
    *token_len = line_len;
    *is_blank = false;
    *is_unknown = false;

    if ((line_len == 7) && (memcmp64(line, "<blank>", 7) == 0)) {
        *token_text = "";
        *token_len = 0;
        *is_blank = true;
        return true;
    }
    if ((line_len == 7) && (memcmp64(line, "<space>", 7) == 0)) {
        *token_text = " ";
        *token_len = 1;
        return true;
    }
    if ((line_len == 5) && (memcmp64(line, "<tab>", 5) == 0)) {
        *token_text = "\t";
        *token_len = 1;
        return true;
    }
    if ((line_len == 5) && (memcmp64(line, "<unk>", 5) == 0)) {
        *token_text = line;
        *token_len = line_len;
        *is_unknown = true;
        return true;
    }

    return true;
}

static bool
lrc_ctc_tokenizer_token_id(
    LrcCtcTokenizer *tokenizer,
    char *token,
    int32 token_len,
    int32 *id
) {
    if ((tokenizer == NULL) || (token == NULL) || (id == NULL)) {
        return false;
    }
    if (token_len < 0) {
        return false;
    }

    for (int32 i = 0; i < tokenizer->token_count; i += 1) {
        LrcCtcToken *item;

        item = tokenizer->tokens + i;
        if ((item->text_len == token_len)
            && (memcmp64(item->text, token, token_len) == 0)) {
            *id = item->id;
            return true;
        }
    }

    return false;
}

static LrcCtcToken *
lrc_ctc_tokenizer_id_to_token(LrcCtcTokenizer *tokenizer, int32 id) {
    if (tokenizer == NULL) {
        return NULL;
    }
    if ((id < 0) || (id >= tokenizer->token_count)) {
        return NULL;
    }

    return tokenizer->tokens + id;
}

static bool
lrc_ctc_tokenizer_add_token(
    LrcCtcTokenizer *tokenizer,
    char *token_text,
    int32 token_len,
    bool is_blank,
    bool is_unknown,
    char *path,
    int32 line_index,
    LrcCtcTokenizerResult *result
) {
    LrcCtcToken *token;
    char *stored_text;
    int32 existing_id;
    int32 id;

    if ((!is_blank && (token_len <= 0)) || (token_len < 0)) {
        lrc_ctc_tokenizer_result_set(
            result,
            LRC_CTC_TOKENIZER_ERROR_EMPTY_TOKEN,
            "CTC tokenizer contains an empty token",
            path,
            line_index,
            -1
        );
        return false;
    }
    if (tokenizer->token_count >= MAXOF(tokenizer->token_count)) {
        lrc_ctc_tokenizer_result_set(
            result,
            LRC_CTC_TOKENIZER_ERROR_TOO_MANY_TOKENS,
            "CTC tokenizer contains too many tokens",
            path,
            line_index,
            -1
        );
        return false;
    }
    if (lrc_ctc_tokenizer_token_id(tokenizer, token_text, token_len,
                                   &existing_id)) {
        lrc_ctc_tokenizer_result_set(
            result,
            LRC_CTC_TOKENIZER_ERROR_DUPLICATE_TOKEN,
            "CTC tokenizer contains a duplicate token",
            path,
            line_index,
            existing_id
        );
        return false;
    }
    if (!lrc_ctc_tokenizer_reserve_tokens(tokenizer, 1)) {
        lrc_ctc_tokenizer_result_set(
            result,
            LRC_CTC_TOKENIZER_ERROR_TOO_MANY_TOKENS,
            "CTC tokenizer contains too many tokens",
            path,
            line_index,
            -1
        );
        return false;
    }
    if (!lrc_ctc_tokenizer_reserve_text(tokenizer, token_len + 1)) {
        lrc_ctc_tokenizer_result_set(
            result,
            LRC_CTC_TOKENIZER_ERROR_TOO_MANY_TOKENS,
            "CTC tokenizer token text is too large",
            path,
            line_index,
            -1
        );
        return false;
    }

    id = tokenizer->token_count;
    stored_text = tokenizer->text_storage + tokenizer->text_storage_len;
    if (token_len > 0) {
        memcpy64(stored_text, token_text, token_len);
    }
    stored_text[token_len] = '\0';
    tokenizer->text_storage_len += token_len + 1;

    token = tokenizer->tokens + tokenizer->token_count;
    token->text = stored_text;
    token->text_len = token_len;
    token->id = id;
    token->is_blank = is_blank;
    token->is_unknown = is_unknown;

    tokenizer->token_count += 1;

    if (is_blank) {
        tokenizer->blank_id = id;
    }
    if (is_unknown) {
        tokenizer->unknown_id = id;
    }

    return true;
}

static bool
lrc_ctc_tokenizer_parse_text(
    LrcCtcTokenizer *tokenizer,
    char *text,
    int32 text_len,
    char *path,
    LrcCtcTokenizerResult *result
) {
    int32 line_start;
    int32 line_index;

    line_start = 0;
    line_index = 0;
    for (int32 i = 0; i <= text_len; i += 1) {
        if ((i == text_len) || (text[i] == '\n')) {
            char *line;
            char *token_text;
            bool is_blank;
            bool is_unknown;
            int32 line_len;
            int32 token_len;

            line = text + line_start;
            line_len = i - line_start;
            if ((line_len > 0) && (line[line_len - 1] == '\r')) {
                line_len -= 1;
            }

            if ((line_len > 0) || (i < text_len)) {
                if (!lrc_ctc_tokenizer_decode_marker(line, line_len,
                                                     &token_text, &token_len,
                                                     &is_blank, &is_unknown)) {
                    return false;
                }
                if (!lrc_ctc_tokenizer_add_token(tokenizer, token_text,
                                                 token_len, is_blank,
                                                 is_unknown, path, line_index,
                                                 result)) {
                    return false;
                }

                line_index += 1;
            }
            line_start = i + 1;
        }
    }

    if (tokenizer->token_count <= 0) {
        lrc_ctc_tokenizer_result_set(
            result,
            LRC_CTC_TOKENIZER_ERROR_EMPTY,
            "CTC tokenizer file is empty",
            path,
            -1,
            -1
        );
        return false;
    }
    if (tokenizer->blank_id < 0) {
        lrc_ctc_tokenizer_result_set(
            result,
            LRC_CTC_TOKENIZER_ERROR_MISSING_BLANK,
            "CTC tokenizer file does not define <blank>",
            path,
            -1,
            -1
        );
        return false;
    }

    return true;
}

static bool
lrc_ctc_tokenizer_load_file(
    LrcCtcTokenizer *tokenizer,
    char *path,
    LrcCtcTokenizerResult *result
) {
    char *file_text;
    int32 file_len;
    int32 bad_offset;
    bool loaded;

    if (result) {
        lrc_ctc_tokenizer_result_init(result);
    }
    if (tokenizer == NULL) {
        lrc_ctc_tokenizer_result_set(
            result,
            LRC_CTC_TOKENIZER_ERROR_INVALID_ARGUMENT,
            "CTC tokenizer load received invalid arguments",
            path,
            -1,
            -1
        );
        return false;
    }

    lrc_ctc_tokenizer_destroy(tokenizer);

    if (lrc_ctc_tokenizer_path_missing(path)) {
        lrc_ctc_tokenizer_result_set(
            result,
            LRC_CTC_TOKENIZER_ERROR_MISSING_PATH,
            "CTC tokenizer path is missing",
            path,
            -1,
            -1
        );
        return false;
    }
    if (!lrc_ctc_tokenizer_read_file(path, &file_text, &file_len, result)) {
        return false;
    }
    if (!lrc_ctc_tokenizer_utf8_valid(file_text, file_len, &bad_offset)) {
        lrc_ctc_tokenizer_result_set(
            result,
            LRC_CTC_TOKENIZER_ERROR_INVALID_UTF8,
            "CTC tokenizer file is not valid UTF-8",
            path,
            bad_offset,
            -1
        );
        free2(file_text, (file_len + 1)*SIZEOF(*file_text));
        return false;
    }

    loaded = lrc_ctc_tokenizer_parse_text(tokenizer, file_text, file_len,
                                          path, result);
    free2(file_text, (file_len + 1)*SIZEOF(*file_text));
    if (!loaded) {
        lrc_ctc_tokenizer_destroy(tokenizer);
        return false;
    }

    return true;
}

#if TESTING_ctc_tokenizer

#define CBASE_IMPLEMENT
#include "cbase.h"

static int32
ctc_tokenizer_test_fail(char *name) {
    error2("CTC tokenizer test failed: %s\n", name);

    return 1;
}

static void
ctc_tokenizer_join_path(
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
ctc_tokenizer_write_file(char *path, char *text) {
    return write_entire_file(path, text, strlen32(text));
}

static int32
ctc_tokenizer_test_load_minimal_vocabulary(void) {
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizerResult result;
    LrcCtcToken *token;
    char temp_dir[PATH_MAX];
    char path[PATH_MAX];
    int32 id;

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "ctc_tokenizer_minimal");
    ctc_tokenizer_join_path(path, SIZEOF(path), temp_dir, "tokens.txt");
    if (!ctc_tokenizer_write_file(path,
                                  "<blank>\n<space>\na\nb\nc\n<unk>\n")) {
        test_remove_tree(temp_dir);
        return ctc_tokenizer_test_fail("write minimal vocabulary");
    }

    lrc_ctc_tokenizer_init(&tokenizer);
    if (!lrc_ctc_tokenizer_load_file(&tokenizer, path, &result)) {
        test_remove_tree(temp_dir);
        return ctc_tokenizer_test_fail("load minimal vocabulary");
    }

    ASSERT(result.error == LRC_CTC_TOKENIZER_ERROR_NONE);
    ASSERT(tokenizer.token_count == 6);
    ASSERT(tokenizer.blank_id == 0);
    ASSERT(tokenizer.unknown_id == 5);

    ASSERT(lrc_ctc_tokenizer_token_id(&tokenizer, " ", 1, &id));
    ASSERT(id == 1);
    ASSERT(lrc_ctc_tokenizer_token_id(&tokenizer, "a", 1, &id));
    ASSERT(id == 2);
    ASSERT(!lrc_ctc_tokenizer_token_id(&tokenizer, "z", 1, &id));

    token = lrc_ctc_tokenizer_id_to_token(&tokenizer, 0);
    ASSERT(token != NULL);
    ASSERT(token->is_blank);
    ASSERT(token->text_len == 0);

    token = lrc_ctc_tokenizer_id_to_token(&tokenizer, 1);
    ASSERT(token != NULL);
    ASSERT(!token->is_blank);
    ASSERT(token->text_len == 1);
    ASSERT(token->text[0] == ' ');

    token = lrc_ctc_tokenizer_id_to_token(&tokenizer, 5);
    ASSERT(token != NULL);
    ASSERT(token->is_unknown);
    ASSERT(strequal(token->text, "<unk>"));

    ASSERT(lrc_ctc_tokenizer_id_to_token(&tokenizer, -1) == NULL);
    ASSERT(lrc_ctc_tokenizer_id_to_token(&tokenizer, 6) == NULL);

    lrc_ctc_tokenizer_destroy(&tokenizer);
    test_remove_tree(temp_dir);

    return 0;
}

static int32
ctc_tokenizer_test_rejects_duplicate_tokens(void) {
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizerResult result;
    char temp_dir[PATH_MAX];
    char path[PATH_MAX];

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "ctc_tokenizer_dup");
    ctc_tokenizer_join_path(path, SIZEOF(path), temp_dir, "tokens.txt");
    if (!ctc_tokenizer_write_file(path, "<blank>\na\n<space>\na\n")) {
        test_remove_tree(temp_dir);
        return ctc_tokenizer_test_fail("write duplicate vocabulary");
    }

    lrc_ctc_tokenizer_init(&tokenizer);
    if (lrc_ctc_tokenizer_load_file(&tokenizer, path, &result)) {
        lrc_ctc_tokenizer_destroy(&tokenizer);
        test_remove_tree(temp_dir);
        return ctc_tokenizer_test_fail("duplicate vocabulary accepted");
    }

    ASSERT(result.error == LRC_CTC_TOKENIZER_ERROR_DUPLICATE_TOKEN);
    ASSERT(result.line_index == 3);
    ASSERT(result.token_id == 1);
    ASSERT(tokenizer.token_count == 0);

    lrc_ctc_tokenizer_destroy(&tokenizer);
    test_remove_tree(temp_dir);

    return 0;
}

static int32
ctc_tokenizer_test_requires_blank_token(void) {
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizerResult result;
    char temp_dir[PATH_MAX];
    char path[PATH_MAX];

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "ctc_tokenizer_blank");
    ctc_tokenizer_join_path(path, SIZEOF(path), temp_dir, "tokens.txt");
    if (!ctc_tokenizer_write_file(path, "a\nb\n<space>\n")) {
        test_remove_tree(temp_dir);
        return ctc_tokenizer_test_fail("write no-blank vocabulary");
    }

    lrc_ctc_tokenizer_init(&tokenizer);
    if (lrc_ctc_tokenizer_load_file(&tokenizer, path, &result)) {
        lrc_ctc_tokenizer_destroy(&tokenizer);
        test_remove_tree(temp_dir);
        return ctc_tokenizer_test_fail("no-blank vocabulary accepted");
    }

    ASSERT(result.error == LRC_CTC_TOKENIZER_ERROR_MISSING_BLANK);
    ASSERT(tokenizer.token_count == 0);

    lrc_ctc_tokenizer_destroy(&tokenizer);
    test_remove_tree(temp_dir);

    return 0;
}

static int32
ctc_tokenizer_test_rejects_empty_token_line(void) {
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizerResult result;
    char temp_dir[PATH_MAX];
    char path[PATH_MAX];

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "ctc_tokenizer_empty");
    ctc_tokenizer_join_path(path, SIZEOF(path), temp_dir, "tokens.txt");
    if (!ctc_tokenizer_write_file(path, "<blank>\na\n\nb\n")) {
        test_remove_tree(temp_dir);
        return ctc_tokenizer_test_fail("write empty-token vocabulary");
    }

    lrc_ctc_tokenizer_init(&tokenizer);
    if (lrc_ctc_tokenizer_load_file(&tokenizer, path, &result)) {
        lrc_ctc_tokenizer_destroy(&tokenizer);
        test_remove_tree(temp_dir);
        return ctc_tokenizer_test_fail("empty-token vocabulary accepted");
    }

    ASSERT(result.error == LRC_CTC_TOKENIZER_ERROR_EMPTY_TOKEN);
    ASSERT(result.line_index == 2);
    ASSERT(tokenizer.token_count == 0);

    lrc_ctc_tokenizer_destroy(&tokenizer);
    test_remove_tree(temp_dir);

    return 0;
}

static int32
ctc_tokenizer_test_rejects_invalid_utf8(void) {
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizerResult result;
    char temp_dir[PATH_MAX];
    char path[PATH_MAX];
    char invalid[] = {
        '<', 'b', 'l', 'a', 'n', 'k', '>', '\n',
        (char)0xC3,
        '\n',
        '\0',
    };

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "ctc_tokenizer_utf8");
    ctc_tokenizer_join_path(path, SIZEOF(path), temp_dir, "tokens.txt");
    if (!write_entire_file(path, invalid, SIZEOF(invalid) - 1)) {
        test_remove_tree(temp_dir);
        return ctc_tokenizer_test_fail("write invalid UTF-8 vocabulary");
    }

    lrc_ctc_tokenizer_init(&tokenizer);
    if (lrc_ctc_tokenizer_load_file(&tokenizer, path, &result)) {
        lrc_ctc_tokenizer_destroy(&tokenizer);
        test_remove_tree(temp_dir);
        return ctc_tokenizer_test_fail("invalid UTF-8 vocabulary accepted");
    }

    ASSERT(result.error == LRC_CTC_TOKENIZER_ERROR_INVALID_UTF8);
    ASSERT(result.line_index == 8);
    ASSERT(tokenizer.token_count == 0);

    lrc_ctc_tokenizer_destroy(&tokenizer);
    test_remove_tree(temp_dir);

    return 0;
}

static int32
ctc_tokenizer_test_missing_path(void) {
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizerResult result;

    lrc_ctc_tokenizer_init(&tokenizer);
    if (lrc_ctc_tokenizer_load_file(&tokenizer, NULL, &result)) {
        return ctc_tokenizer_test_fail("missing path accepted");
    }

    ASSERT(result.error == LRC_CTC_TOKENIZER_ERROR_MISSING_PATH);
    ASSERT(result.path == NULL);
    ASSERT(tokenizer.token_count == 0);

    lrc_ctc_tokenizer_destroy(&tokenizer);

    return 0;
}

int32
main(void) {
    if (ctc_tokenizer_test_load_minimal_vocabulary() != 0) {
        exit(1);
    }
    if (ctc_tokenizer_test_rejects_duplicate_tokens() != 0) {
        exit(1);
    }
    if (ctc_tokenizer_test_requires_blank_token() != 0) {
        exit(1);
    }
    if (ctc_tokenizer_test_rejects_empty_token_line() != 0) {
        exit(1);
    }
    if (ctc_tokenizer_test_rejects_invalid_utf8() != 0) {
        exit(1);
    }
    if (ctc_tokenizer_test_missing_path() != 0) {
        exit(1);
    }

    return 0;
}

#endif /* TESTING_ctc_tokenizer */
