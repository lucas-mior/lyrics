#include "lyricsync.h"
#include "ctc_tokenizer.h"

#include "cbase.h"

#if !defined(TESTING_ctc_tokenizer)
#define TESTING_ctc_tokenizer 0
#endif

static void
lrc_ctc_tokenize_result_init(LrcCtcTokenizeResult *result) {
    if (result == NULL) {
        return;
    }

    result->error = LRC_CTC_TOKENIZE_ERROR_NONE;
    result->message = "ok";

    result->byte_offset = -1;
    result->line_index = -1;
    result->token_id = -1;

    return;
}

static void
lrc_ctc_tokenize_result_set(
    LrcCtcTokenizeResult *result,
    enum LrcCtcTokenizeError error,
    char *message,
    int32 byte_offset,
    int32 line_index,
    int32 token_id
) {
    if (result == NULL) {
        return;
    }

    result->error = error;
    result->message = message;

    result->byte_offset = byte_offset;
    result->line_index = line_index;
    result->token_id = token_id;

    return;
}

static void
lrc_ctc_tokenized_text_init(LrcCtcTokenizedText *text) {
    if (text == NULL) {
        return;
    }

    memset64(text, 0, SIZEOF(*text));

    return;
}

static void
lrc_ctc_tokenized_text_destroy(LrcCtcTokenizedText *text) {
    if (text == NULL) {
        return;
    }

    if (text->tokens) {
        free2(text->tokens, text->token_cap*SIZEOF(*text->tokens));
    }

    lrc_ctc_tokenized_text_init(text);

    return;
}

static bool
lrc_ctc_tokenized_text_reserve(
    LrcCtcTokenizedText *text,
    int32 extra
) {
    int64 needed;
    int32 new_cap;

    if (extra <= 0) {
        return true;
    }

    needed = (int64)text->token_count + extra;
    if (needed <= text->token_cap) {
        return true;
    }
    if (needed >= MAXOF(text->token_cap)) {
        return false;
    }

    new_cap = text->token_cap;
    if (new_cap <= 0) {
        new_cap = 32;
    }
    while (new_cap < needed) {
        if (new_cap >= (MAXOF(new_cap)/2)) {
            new_cap = (int32)needed;
            break;
        }
        new_cap *= 2;
    }

    text->tokens = realloc2(text->tokens,
                            text->token_cap,
                            new_cap,
                            SIZEOF(*text->tokens));
    text->token_cap = new_cap;

    return true;
}

static bool
lrc_ctc_tokenized_text_append(
    LrcCtcTokenizedText *text,
    int32 token_id,
    int32 start,
    int32 end,
    int32 line_index,
    int32 segment_index,
    bool starts_segment
) {
    LrcCtcTextToken *token;

    if (!lrc_ctc_tokenized_text_reserve(text, 1)) {
        return false;
    }

    token = text->tokens + text->token_count;
    text->token_count += 1;

    token->token_id = token_id;
    token->normalized_start = start;
    token->normalized_end = end;
    token->line_index = line_index;
    token->segment_index = segment_index;
    token->starts_segment = starts_segment;

    return true;
}

static int32
lrc_ctc_tokenizer_utf8_step(char *text, int32 text_len, int32 offset) {
    uint32 rune;
    int32 step;

    if ((text == NULL) || (offset < 0) || (offset >= text_len)) {
        return 0;
    }

    step = utf8_decode_raw(text + offset, &rune, text_len - offset);
    if (step <= 0) {
        return 1;
    }

    return step;
}

static bool
lrc_ctc_tokenizer_best_match(
    LrcCtcTokenizer *tokenizer,
    char *text,
    int32 text_len,
    int32 offset,
    int32 *token_id,
    int32 *token_len
) {
    int32 best_id;
    int32 best_len;

    best_id = -1;
    best_len = -1;
    for (int32 i = 0; i < tokenizer->token_count; i += 1) {
        LrcCtcToken *token;

        token = tokenizer->tokens + i;
        if (token->is_blank || token->is_unknown) {
            continue;
        }
        if (token->text_len <= best_len) {
            continue;
        }
        if ((offset + token->text_len) > text_len) {
            continue;
        }
        if (memcmp64(text + offset, token->text, token->text_len) != 0) {
            continue;
        }

        best_id = token->id;
        best_len = token->text_len;
    }

    if (best_id < 0) {
        return false;
    }

    *token_id = best_id;
    *token_len = best_len;

    return true;
}


static bool
lrc_ctc_tokenizer_is_unmatched_separator(
    char *text,
    int32 text_len,
    int32 offset,
    int32 *skip_len
) {
    if ((text == NULL) || (offset < 0) || (offset >= text_len)) {
        return false;
    }
    if (text[offset] != ' ') {
        return false;
    }

    if (skip_len) {
        *skip_len = 1;
    }

    return true;
}

static bool
lrc_ctc_tokenizer_uses_target_text(LrcLyricsNormalized *normalized) {
    if (normalized == NULL) {
        return false;
    }
    if ((normalized->target_text == NULL)
        || (normalized->target_text_len <= 0)) {
        return false;
    }

    return true;
}

static int32
lrc_ctc_tokenizer_map_line_at(
    LrcLyricsNormalized *normalized,
    int32 offset,
    bool use_target_text
) {
    if (normalized == NULL) {
        return -1;
    }
    if (use_target_text) {
        if ((offset < 0) || (offset >= normalized->target_byte_count)) {
            return -1;
        }
        return normalized->target_bytes[offset].line_index;
    }
    if ((offset < 0) || (offset >= normalized->byte_count)) {
        return -1;
    }

    return normalized->bytes[offset].line_index;
}

static bool
lrc_ctc_tokenizer_map_normalized_range(
    LrcLyricsNormalized *normalized,
    int32 offset,
    int32 token_len,
    bool use_target_text,
    int32 *normalized_start,
    int32 *normalized_end,
    int32 *line_index
) {
    int32 last;

    if ((normalized_start == NULL) || (normalized_end == NULL)
        || (line_index == NULL)) {
        return false;
    }

    *normalized_start = -1;
    *normalized_end = -1;
    *line_index = -1;

    if (token_len <= 0) {
        return false;
    }
    last = offset + token_len - 1;
    if (use_target_text) {
        if ((offset < 0) || (last >= normalized->target_byte_count)) {
            return false;
        }
        *normalized_start = normalized->target_bytes[offset].normalized_start;
        *normalized_end = normalized->target_bytes[last].normalized_end;
        *line_index = normalized->target_bytes[offset].line_index;
        return true;
    }
    if ((offset < 0) || (last >= normalized->byte_count)) {
        return false;
    }

    *normalized_start = offset;
    *normalized_end = offset + token_len;
    *line_index = normalized->bytes[offset].line_index;

    return true;
}

static int32
lrc_ctc_tokenizer_segment_index_for_range(
    LrcLyricsNormalized *normalized,
    int32 normalized_start,
    int32 normalized_end
) {
    if (normalized == NULL) {
        return -1;
    }
    if ((normalized_start < 0) || (normalized_end <= normalized_start)) {
        return -1;
    }

    for (int32 i = 0; i < normalized->segment_count; i += 1) {
        CtcTextSegment *segment;

        segment = normalized->segments + i;
        if (segment->normalized_end <= segment->normalized_start) {
            continue;
        }
        if ((normalized_start >= segment->normalized_start)
            && (normalized_end <= segment->normalized_end)) {
            return i;
        }
    }

    return -1;
}

static bool
lrc_ctc_tokenizer_range_starts_segment(
    LrcLyricsNormalized *normalized,
    int32 segment_index,
    int32 normalized_start
) {
    CtcTextSegment *segment;

    if (normalized == NULL) {
        return false;
    }
    if ((segment_index < 0) || (segment_index >= normalized->segment_count)) {
        return false;
    }

    segment = normalized->segments + segment_index;
    return normalized_start == segment->normalized_start;
}

static bool
lrc_ctc_tokenizer_tokenize_normalized(
    LrcCtcTokenizer *tokenizer,
    LrcLyricsNormalized *normalized,
    LrcCtcTokenizedText *tokens,
    LrcCtcTokenizeResult *result
) {
    char *text;
    int32 text_len;
    bool use_target_text;

    if (result) {
        lrc_ctc_tokenize_result_init(result);
    }
    if ((tokenizer == NULL) || (normalized == NULL) || (tokens == NULL)) {
        lrc_ctc_tokenize_result_set(
            result,
            LRC_CTC_TOKENIZE_ERROR_INVALID_ARGUMENT,
            "CTC tokenization received invalid arguments",
            -1,
            -1,
            -1
        );
        return false;
    }

    lrc_ctc_tokenized_text_destroy(tokens);

    use_target_text = lrc_ctc_tokenizer_uses_target_text(normalized);
    if (use_target_text) {
        text = normalized->target_text;
        text_len = normalized->target_text_len;
    } else {
        text = normalized->text;
        text_len = normalized->text_len;
    }
    if ((text == NULL) || (text_len <= 0)) {
        lrc_ctc_tokenize_result_set(
            result,
            LRC_CTC_TOKENIZE_ERROR_EMPTY_INPUT,
            "normalized lyrics are empty",
            -1,
            -1,
            -1
        );
        return false;
    }

    for (int32 i = 0; i < text_len;) {
        int32 token_id;
        int32 token_len;
        int32 normalized_start;
        int32 normalized_end;
        int32 line_index;
        int32 segment_index;
        bool starts_segment;

        line_index = lrc_ctc_tokenizer_map_line_at(normalized,
                                                   i,
                                                   use_target_text);
        if (use_target_text
            && lrc_ctc_tokenizer_is_unmatched_separator(text,
                                                        text_len,
                                                        i,
                                                        &token_len)) {
            i += token_len;
            continue;
        }
        if (lrc_ctc_tokenizer_best_match(tokenizer,
                                         text,
                                         text_len,
                                         i,
                                         &token_id,
                                         &token_len)) {
            if (!lrc_ctc_tokenizer_map_normalized_range(normalized,
                                                         i,
                                                         token_len,
                                                         use_target_text,
                                                         &normalized_start,
                                                         &normalized_end,
                                                         &line_index)) {
                lrc_ctc_tokenize_result_set(
                    result,
                    LRC_CTC_TOKENIZE_ERROR_INVALID_ARGUMENT,
                    "CTC token mapping failed",
                    i,
                    line_index,
                    token_id
                );
                lrc_ctc_tokenized_text_destroy(tokens);
                return false;
            }
            segment_index = lrc_ctc_tokenizer_segment_index_for_range(
                normalized,
                normalized_start,
                normalized_end
            );
            starts_segment = lrc_ctc_tokenizer_range_starts_segment(
                normalized,
                segment_index,
                normalized_start
            );
            if (!lrc_ctc_tokenized_text_append(tokens,
                                               token_id,
                                               normalized_start,
                                               normalized_end,
                                               line_index,
                                               segment_index,
                                               starts_segment)) {
                lrc_ctc_tokenize_result_set(
                    result,
                    LRC_CTC_TOKENIZE_ERROR_TOO_MANY_TOKENS,
                    "tokenized lyrics contain too many tokens",
                    i,
                    line_index,
                    token_id
                );
                lrc_ctc_tokenized_text_destroy(tokens);
                return false;
            }
            i += token_len;
            continue;
        }

        if (lrc_ctc_tokenizer_is_unmatched_separator(text,
                                                     text_len,
                                                     i,
                                                     &token_len)) {
            i += token_len;
            continue;
        }

        if (tokenizer->unknown_id >= 0) {
            token_id = tokenizer->unknown_id;
            token_len = lrc_ctc_tokenizer_utf8_step(text, text_len, i);
            if (!lrc_ctc_tokenizer_map_normalized_range(normalized,
                                                         i,
                                                         token_len,
                                                         use_target_text,
                                                         &normalized_start,
                                                         &normalized_end,
                                                         &line_index)) {
                lrc_ctc_tokenize_result_set(
                    result,
                    LRC_CTC_TOKENIZE_ERROR_INVALID_ARGUMENT,
                    "CTC token mapping failed",
                    i,
                    line_index,
                    token_id
                );
                lrc_ctc_tokenized_text_destroy(tokens);
                return false;
            }
            segment_index = lrc_ctc_tokenizer_segment_index_for_range(
                normalized,
                normalized_start,
                normalized_end
            );
            starts_segment = lrc_ctc_tokenizer_range_starts_segment(
                normalized,
                segment_index,
                normalized_start
            );
            if (!lrc_ctc_tokenized_text_append(tokens,
                                               token_id,
                                               normalized_start,
                                               normalized_end,
                                               line_index,
                                               segment_index,
                                               starts_segment)) {
                lrc_ctc_tokenize_result_set(
                    result,
                    LRC_CTC_TOKENIZE_ERROR_TOO_MANY_TOKENS,
                    "tokenized lyrics contain too many tokens",
                    i,
                    line_index,
                    token_id
                );
                lrc_ctc_tokenized_text_destroy(tokens);
                return false;
            }
            i += token_len;
            continue;
        }

        lrc_ctc_tokenize_result_set(
            result,
            LRC_CTC_TOKENIZE_ERROR_UNSUPPORTED_TOKEN,
            "normalized lyrics contain a token not in the CTC vocabulary",
            i,
            line_index,
            -1
        );
        lrc_ctc_tokenized_text_destroy(tokens);
        return false;
    }

    if (tokens->token_count <= 0) {
        lrc_ctc_tokenize_result_set(
            result,
            LRC_CTC_TOKENIZE_ERROR_EMPTY_INPUT,
            "normalized lyrics produced no CTC tokens",
            -1,
            -1,
            -1
        );
        return false;
    }

    return true;
}

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
    if ((tokenizer == NULL) || (tokenizer->tokens == NULL)) {
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
#include "lyrics.c"
#include "unicode_norm.c"
#include "ctc_text.c"

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


static bool
ctc_tokenizer_load_from_text(
    LrcCtcTokenizer *tokenizer,
    char *text,
    char *name
) {
    LrcCtcTokenizerResult result;
    char temp_dir[PATH_MAX];
    char path[PATH_MAX];
    bool ok;

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), name);
    ctc_tokenizer_join_path(path, SIZEOF(path), temp_dir, "tokens.txt");
    if (!ctc_tokenizer_write_file(path, text)) {
        test_remove_tree(temp_dir);
        return false;
    }

    lrc_ctc_tokenizer_init(tokenizer);
    ok = lrc_ctc_tokenizer_load_file(tokenizer, path, &result);
    test_remove_tree(temp_dir);

    return ok;
}

static bool
ctc_tokenizer_normalize_lyrics_text(
    LrcLyrics *lyrics,
    LrcLyricsNormalized *normalized,
    char *text,
    char *name
) {
    LrcLyricsLoadResult result;
    LrcLyricsPreprocessOptions options;
    char temp_dir[PATH_MAX];
    char path[PATH_MAX];
    bool ok;

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), name);
    ctc_tokenizer_join_path(path, SIZEOF(path), temp_dir, "lyrics.txt");
    if (!write_entire_file(path, text, strlen32(text))) {
        test_remove_tree(temp_dir);
        return false;
    }

    lrc_lyrics_init(lyrics);
    lrc_lyrics_normalized_init(normalized);
    ok = lrc_lyrics_load_file(lyrics, path, &result);
    test_remove_tree(temp_dir);
    if (!ok) {
        return false;
    }

    lrc_lyrics_preprocess_options_init(&options);
    options.split_size = LRC_LYRICS_PREPROCESS_SPLIT_SIZE_CURRENT;
    options.romanization = LRC_LYRICS_PREPROCESS_ROMANIZATION_OFF;

    return lrc_lyrics_normalize_with_options(lyrics, normalized, &options);
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
ctc_tokenizer_test_tokenizes_normalized_text(void) {
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizedText tokens;
    LrcCtcTokenizeResult result;
    LrcLyrics lyrics;
    LrcLyricsNormalized normalized;
    char tokenizer_text[] = "<blank>\n<space>\nca\nc\na\nt\n";
    char lyrics_text[] = "Cat\nCA!\n";

    if (!ctc_tokenizer_load_from_text(&tokenizer,
                                      tokenizer_text,
                                      "ctc_tokenizer_tokenize")) {
        return ctc_tokenizer_test_fail("load tokenization vocabulary");
    }
    if (!ctc_tokenizer_normalize_lyrics_text(&lyrics,
                                             &normalized,
                                             lyrics_text,
                                             "ctc_tokenizer_lyrics")) {
        lrc_ctc_tokenizer_destroy(&tokenizer);
        return ctc_tokenizer_test_fail("normalize tokenization lyrics");
    }

    lrc_ctc_tokenized_text_init(&tokens);
    if (!lrc_ctc_tokenizer_tokenize_normalized(&tokenizer,
                                               &normalized,
                                               &tokens,
                                               &result)) {
        lrc_lyrics_normalized_destroy(&normalized);
        lrc_lyrics_destroy(&lyrics);
        lrc_ctc_tokenizer_destroy(&tokenizer);
        return ctc_tokenizer_test_fail("tokenize normalized lyrics");
    }

    ASSERT(result.error == LRC_CTC_TOKENIZE_ERROR_NONE);
    ASSERT(tokens.token_count == 4);
    ASSERT(tokens.tokens[0].token_id == 2);
    ASSERT(tokens.tokens[0].normalized_start == 0);
    ASSERT(tokens.tokens[0].normalized_end == 2);
    ASSERT(tokens.tokens[0].line_index == 0);
    ASSERT(tokens.tokens[1].token_id == 5);
    ASSERT(tokens.tokens[1].normalized_start == 2);
    ASSERT(tokens.tokens[1].normalized_end == 3);
    ASSERT(tokens.tokens[1].line_index == 0);
    ASSERT(tokens.tokens[2].token_id == 1);
    ASSERT(tokens.tokens[2].normalized_start == 3);
    ASSERT(tokens.tokens[2].normalized_end == 4);
    ASSERT(tokens.tokens[2].line_index == 1);
    ASSERT(tokens.tokens[3].token_id == 2);
    ASSERT(tokens.tokens[3].normalized_start == 4);
    ASSERT(tokens.tokens[3].normalized_end == 6);
    ASSERT(tokens.tokens[3].line_index == 1);

    lrc_ctc_tokenized_text_destroy(&tokens);
    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);
    lrc_ctc_tokenizer_destroy(&tokenizer);

    return 0;
}

static int32
ctc_tokenizer_test_rejects_unsupported_normalized_token(void) {
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizedText tokens;
    LrcCtcTokenizeResult result;
    LrcLyrics lyrics;
    LrcLyricsNormalized normalized;
    char tokenizer_text[] = "<blank>\n<space>\na\n";
    char lyrics_text[] = "a z\n";

    if (!ctc_tokenizer_load_from_text(&tokenizer,
                                      tokenizer_text,
                                      "ctc_tokenizer_bad_token")) {
        return ctc_tokenizer_test_fail("load unsupported vocabulary");
    }
    if (!ctc_tokenizer_normalize_lyrics_text(&lyrics,
                                             &normalized,
                                             lyrics_text,
                                             "ctc_tokenizer_bad_lyrics")) {
        lrc_ctc_tokenizer_destroy(&tokenizer);
        return ctc_tokenizer_test_fail("normalize unsupported lyrics");
    }

    lrc_ctc_tokenized_text_init(&tokens);
    if (lrc_ctc_tokenizer_tokenize_normalized(&tokenizer,
                                              &normalized,
                                              &tokens,
                                              &result)) {
        lrc_ctc_tokenized_text_destroy(&tokens);
        lrc_lyrics_normalized_destroy(&normalized);
        lrc_lyrics_destroy(&lyrics);
        lrc_ctc_tokenizer_destroy(&tokenizer);
        return ctc_tokenizer_test_fail("unsupported token accepted");
    }

    ASSERT(result.error == LRC_CTC_TOKENIZE_ERROR_UNSUPPORTED_TOKEN);
    ASSERT(result.byte_offset == 2);
    ASSERT(result.line_index == 0);
    ASSERT(result.token_id == -1);
    ASSERT(tokens.token_count == 0);

    lrc_ctc_tokenized_text_destroy(&tokens);
    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);
    lrc_ctc_tokenizer_destroy(&tokenizer);

    return 0;
}

static int32
ctc_tokenizer_test_unknown_token_covers_one_utf8_rune(void) {
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizedText tokens;
    LrcCtcTokenizeResult result;
    LrcLyrics lyrics;
    LrcLyricsNormalized normalized;
    char tokenizer_text[] = "<blank>\n<space>\na\n<unk>\n";
    char lyrics_text[] = "a é\n";

    if (!ctc_tokenizer_load_from_text(&tokenizer,
                                      tokenizer_text,
                                      "ctc_tokenizer_unknown")) {
        return ctc_tokenizer_test_fail("load unknown vocabulary");
    }
    if (!ctc_tokenizer_normalize_lyrics_text(&lyrics,
                                             &normalized,
                                             lyrics_text,
                                             "ctc_tokenizer_unknown_lyrics")) {
        lrc_ctc_tokenizer_destroy(&tokenizer);
        return ctc_tokenizer_test_fail("normalize unknown lyrics");
    }

    lrc_ctc_tokenized_text_init(&tokens);
    if (!lrc_ctc_tokenizer_tokenize_normalized(&tokenizer,
                                               &normalized,
                                               &tokens,
                                               &result)) {
        lrc_ctc_tokenized_text_destroy(&tokens);
        lrc_lyrics_normalized_destroy(&normalized);
        lrc_lyrics_destroy(&lyrics);
        lrc_ctc_tokenizer_destroy(&tokenizer);
        return ctc_tokenizer_test_fail("tokenize unknown lyrics");
    }

    ASSERT(tokens.token_count == 3);
    ASSERT(tokens.tokens[0].token_id == 2);
    ASSERT(tokens.tokens[0].normalized_start == 0);
    ASSERT(tokens.tokens[0].normalized_end == 1);
    ASSERT(tokens.tokens[1].token_id == 1);
    ASSERT(tokens.tokens[1].normalized_start == 1);
    ASSERT(tokens.tokens[1].normalized_end == 2);
    ASSERT(tokens.tokens[2].token_id == 3);
    ASSERT(tokens.tokens[2].normalized_start == 2);
    ASSERT(tokens.tokens[2].normalized_end == 4);
    ASSERT(tokens.tokens[2].line_index == 0);

    lrc_ctc_tokenized_text_destroy(&tokens);
    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);
    lrc_ctc_tokenizer_destroy(&tokenizer);

    return 0;
}


static int32
ctc_tokenizer_test_word_target_prevents_multi_character_match(void) {
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizedText tokens;
    LrcCtcTokenizeResult result;
    LrcLyricsPreprocessOptions options;
    LrcLyrics lyrics;
    LrcLyricsNormalized normalized;
    char tokenizer_text[] = "<blank>\nca\nc\na\nt\n";
    char lyrics_text[] = "Cat\n";

    if (!ctc_tokenizer_load_from_text(&tokenizer,
                                      tokenizer_text,
                                      "ctc_tokenizer_word_target")) {
        return ctc_tokenizer_test_fail("load word target vocabulary");
    }
    if (!ctc_tokenizer_normalize_lyrics_text(
        &lyrics,
        &normalized,
        lyrics_text,
        "ctc_tokenizer_word_target_lyrics"
    )) {
        lrc_ctc_tokenizer_destroy(&tokenizer);
        return ctc_tokenizer_test_fail("normalize word target lyrics");
    }

    lrc_lyrics_preprocess_options_init(&options);
    options.split_size = LRC_LYRICS_PREPROCESS_SPLIT_SIZE_WORD;
    if (!lrc_lyrics_normalize_with_options(&lyrics, &normalized, &options)) {
        lrc_lyrics_normalized_destroy(&normalized);
        lrc_lyrics_destroy(&lyrics);
        lrc_ctc_tokenizer_destroy(&tokenizer);
        return ctc_tokenizer_test_fail("normalize word target option lyrics");
    }

    ASSERT(strequal2(normalized.text, normalized.text_len, STRLIT("cat")));
    ASSERT(strequal2(normalized.target_text,
                     normalized.target_text_len,
                     STRLIT("c a t")));

    lrc_ctc_tokenized_text_init(&tokens);
    if (!lrc_ctc_tokenizer_tokenize_normalized(&tokenizer,
                                               &normalized,
                                               &tokens,
                                               &result)) {
        lrc_ctc_tokenized_text_destroy(&tokens);
        lrc_lyrics_normalized_destroy(&normalized);
        lrc_lyrics_destroy(&lyrics);
        lrc_ctc_tokenizer_destroy(&tokenizer);
        return ctc_tokenizer_test_fail("tokenize word target lyrics");
    }

    ASSERT(result.error == LRC_CTC_TOKENIZE_ERROR_NONE);
    ASSERT(tokens.token_count == 3);
    ASSERT(tokens.tokens[0].token_id == 2);
    ASSERT(tokens.tokens[0].normalized_start == 0);
    ASSERT(tokens.tokens[0].normalized_end == 1);
    ASSERT(tokens.tokens[1].token_id == 3);
    ASSERT(tokens.tokens[1].normalized_start == 1);
    ASSERT(tokens.tokens[1].normalized_end == 2);
    ASSERT(tokens.tokens[2].token_id == 4);
    ASSERT(tokens.tokens[2].normalized_start == 2);
    ASSERT(tokens.tokens[2].normalized_end == 3);

    lrc_ctc_tokenized_text_destroy(&tokens);
    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);
    lrc_ctc_tokenizer_destroy(&tokenizer);

    return 0;
}

static int32
ctc_tokenizer_test_marks_segment_starts(void) {
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizedText tokens;
    LrcCtcTokenizeResult result;
    LrcLyricsPreprocessOptions options;
    LrcLyrics lyrics;
    LrcLyricsNormalized normalized;
    char tokenizer_text[] = "<blank>\nh\ni\ny\no\n";
    char lyrics_text[] = "Hi yo\n";

    if (!ctc_tokenizer_load_from_text(&tokenizer,
                                      tokenizer_text,
                                      "ctc_tokenizer_segments")) {
        return ctc_tokenizer_test_fail("load segment start vocabulary");
    }
    if (!ctc_tokenizer_normalize_lyrics_text(
        &lyrics,
        &normalized,
        lyrics_text,
        "ctc_tokenizer_segment_lyrics"
    )) {
        lrc_ctc_tokenizer_destroy(&tokenizer);
        return ctc_tokenizer_test_fail("normalize segment start lyrics");
    }

    lrc_lyrics_preprocess_options_init(&options);
    options.split_size = LRC_LYRICS_PREPROCESS_SPLIT_SIZE_WORD;
    if (!lrc_lyrics_normalize_with_options(&lyrics, &normalized, &options)) {
        lrc_lyrics_normalized_destroy(&normalized);
        lrc_lyrics_destroy(&lyrics);
        lrc_ctc_tokenizer_destroy(&tokenizer);
        return ctc_tokenizer_test_fail("normalize segment word lyrics");
    }

    lrc_ctc_tokenized_text_init(&tokens);
    if (!lrc_ctc_tokenizer_tokenize_normalized(&tokenizer,
                                               &normalized,
                                               &tokens,
                                               &result)) {
        lrc_ctc_tokenized_text_destroy(&tokens);
        lrc_lyrics_normalized_destroy(&normalized);
        lrc_lyrics_destroy(&lyrics);
        lrc_ctc_tokenizer_destroy(&tokenizer);
        return ctc_tokenizer_test_fail("tokenize segment start lyrics");
    }

    ASSERT(tokens.token_count == 4);
    ASSERT(tokens.tokens[0].segment_index == 0);
    ASSERT(tokens.tokens[0].starts_segment);
    ASSERT(tokens.tokens[1].segment_index == 0);
    ASSERT(!tokens.tokens[1].starts_segment);
    ASSERT(tokens.tokens[2].segment_index == 1);
    ASSERT(tokens.tokens[2].starts_segment);
    ASSERT(tokens.tokens[3].segment_index == 1);
    ASSERT(!tokens.tokens[3].starts_segment);

    lrc_ctc_tokenized_text_destroy(&tokens);
    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);
    lrc_ctc_tokenizer_destroy(&tokenizer);

    return 0;
}

static int32
ctc_tokenizer_test_skips_unmatched_spaces(void) {
    LrcCtcTokenizer tokenizer;
    LrcCtcTokenizedText tokens;
    LrcCtcTokenizeResult result;
    LrcLyrics lyrics;
    LrcLyricsNormalized normalized;
    char tokenizer_text[] = "<blank>\n<unk>\na\nb\nc\n";
    char lyrics_text[] = "ab c\n";

    if (!ctc_tokenizer_load_from_text(&tokenizer,
                                      tokenizer_text,
                                      "ctc_tokenizer_skip_spaces")) {
        return ctc_tokenizer_test_fail("load no-space vocabulary");
    }
    if (!ctc_tokenizer_normalize_lyrics_text(&lyrics,
                                             &normalized,
                                             lyrics_text,
                                             "ctc_tokenizer_skip_lyrics")) {
        lrc_ctc_tokenizer_destroy(&tokenizer);
        return ctc_tokenizer_test_fail("normalize no-space lyrics");
    }

    lrc_ctc_tokenized_text_init(&tokens);
    if (!lrc_ctc_tokenizer_tokenize_normalized(&tokenizer,
                                               &normalized,
                                               &tokens,
                                               &result)) {
        lrc_ctc_tokenized_text_destroy(&tokens);
        lrc_lyrics_normalized_destroy(&normalized);
        lrc_lyrics_destroy(&lyrics);
        lrc_ctc_tokenizer_destroy(&tokenizer);
        return ctc_tokenizer_test_fail("tokenize no-space lyrics");
    }

    ASSERT(result.error == LRC_CTC_TOKENIZE_ERROR_NONE);
    ASSERT(tokens.token_count == 3);
    ASSERT(tokens.tokens[0].token_id == 2);
    ASSERT(tokens.tokens[0].normalized_start == 0);
    ASSERT(tokens.tokens[0].normalized_end == 1);
    ASSERT(tokens.tokens[1].token_id == 3);
    ASSERT(tokens.tokens[1].normalized_start == 1);
    ASSERT(tokens.tokens[1].normalized_end == 2);
    ASSERT(tokens.tokens[2].token_id == 4);
    ASSERT(tokens.tokens[2].normalized_start == 3);
    ASSERT(tokens.tokens[2].normalized_end == 4);

    lrc_ctc_tokenized_text_destroy(&tokens);
    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);
    lrc_ctc_tokenizer_destroy(&tokenizer);

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
        fatal(EXIT_FAILURE);
    }
    if (ctc_tokenizer_test_rejects_duplicate_tokens() != 0) {
        fatal(EXIT_FAILURE);
    }
    if (ctc_tokenizer_test_requires_blank_token() != 0) {
        fatal(EXIT_FAILURE);
    }
    if (ctc_tokenizer_test_rejects_empty_token_line() != 0) {
        fatal(EXIT_FAILURE);
    }
    if (ctc_tokenizer_test_rejects_invalid_utf8() != 0) {
        fatal(EXIT_FAILURE);
    }
    if (ctc_tokenizer_test_missing_path() != 0) {
        fatal(EXIT_FAILURE);
    }
    if (ctc_tokenizer_test_tokenizes_normalized_text() != 0) {
        fatal(EXIT_FAILURE);
    }
    if (ctc_tokenizer_test_rejects_unsupported_normalized_token() != 0) {
        fatal(EXIT_FAILURE);
    }
    if (ctc_tokenizer_test_unknown_token_covers_one_utf8_rune() != 0) {
        fatal(EXIT_FAILURE);
    }
    if (ctc_tokenizer_test_word_target_prevents_multi_character_match()
        != 0) {
        fatal(EXIT_FAILURE);
    }
    if (ctc_tokenizer_test_marks_segment_starts() != 0) {
        fatal(EXIT_FAILURE);
    }
    if (ctc_tokenizer_test_skips_unmatched_spaces() != 0) {
        fatal(EXIT_FAILURE);
    }

    return 0;
}

#endif /* TESTING_ctc_tokenizer */
