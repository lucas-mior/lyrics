#include "lyrics.h"

#include "cbase.h"

#if !defined(TESTING_lyrics)
#define TESTING_lyrics 0
#endif

static void
lrc_lyrics_init(LrcLyrics *lyrics) {
    if (lyrics == NULL) {
        return;
    }

    memset64(lyrics, 0, SIZEOF(*lyrics));

    return;
}

static void
lrc_lyrics_destroy(LrcLyrics *lyrics) {
    if (lyrics == NULL) {
        return;
    }

    if (lyrics->text) {
        free2(lyrics->text, lyrics->text_cap*SIZEOF(*lyrics->text));
    }
    if (lyrics->lines) {
        free2(lyrics->lines, lyrics->line_cap*SIZEOF(*lyrics->lines));
    }

    lrc_lyrics_init(lyrics);

    return;
}

static void
lrc_lyrics_load_result_init(LrcLyricsLoadResult *result) {
    if (result == NULL) {
        return;
    }

    result->error = LRC_LYRICS_LOAD_ERROR_NONE;
    result->message = "ok";
    result->path = NULL;

    result->byte_offset = -1;

    return;
}

static void
lrc_lyrics_load_result_set(
    LrcLyricsLoadResult *result,
    enum LrcLyricsLoadError error,
    char *message,
    char *path,
    int32 byte_offset
) {
    if (result == NULL) {
        return;
    }

    result->error = error;
    result->message = message;
    result->path = path;

    result->byte_offset = byte_offset;

    return;
}

static bool
lrc_lyrics_path_missing(char *path) {
    if (path == NULL) {
        return true;
    }
    if (path[0] == '\0') {
        return true;
    }

    return false;
}

static bool
lrc_lyrics_read_file(
    char *path,
    char **file_text,
    int32 *file_len,
    LrcLyricsLoadResult *result
) {
    FILE *file;
    int64 len;
    int64 read_len;
    char *text;

    if ((file = fopen(path, "rb")) == NULL) {
        lrc_lyrics_load_result_set(
            result,
            LRC_LYRICS_LOAD_ERROR_OPEN_FAILED,
            "could not open lyrics file",
            path,
            -1
        );
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        lrc_lyrics_load_result_set(
            result,
            LRC_LYRICS_LOAD_ERROR_READ_FAILED,
            "could not seek lyrics file",
            path,
            -1
        );
        XFCLOSE(file, path);
        return false;
    }
    if ((len = ftell(file)) < 0) {
        lrc_lyrics_load_result_set(
            result,
            LRC_LYRICS_LOAD_ERROR_READ_FAILED,
            "could not measure lyrics file",
            path,
            -1
        );
        XFCLOSE(file, path);
        return false;
    }
    if (len >= MAXOF(*file_len)) {
        lrc_lyrics_load_result_set(
            result,
            LRC_LYRICS_LOAD_ERROR_FILE_TOO_LARGE,
            "lyrics file is too large",
            path,
            -1
        );
        XFCLOSE(file, path);
        return false;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        lrc_lyrics_load_result_set(
            result,
            LRC_LYRICS_LOAD_ERROR_READ_FAILED,
            "could not rewind lyrics file",
            path,
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
        lrc_lyrics_load_result_set(
            result,
            LRC_LYRICS_LOAD_ERROR_READ_FAILED,
            "could not read lyrics file",
            path,
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
lrc_lyrics_utf8_valid(char *text, int32 text_len, int32 *bad_offset) {
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
lrc_lyrics_has_utf8_bom(char *text, int32 text_len) {
    if (text_len < 3) {
        return false;
    }
    if ((uint8)text[0] != 0xEF) {
        return false;
    }
    if ((uint8)text[1] != 0xBB) {
        return false;
    }
    if ((uint8)text[2] != 0xBF) {
        return false;
    }

    return true;
}

static bool
lrc_lyrics_normalize_text(
    LrcLyrics *lyrics,
    char *file_text,
    int32 file_len,
    LrcLyricsLoadResult *result,
    char *path
) {
    int32 bad_offset;
    int32 start;
    int32 normalized_len;
    char *normalized;

    if (!lrc_lyrics_utf8_valid(file_text, file_len, &bad_offset)) {
        lrc_lyrics_load_result_set(
            result,
            LRC_LYRICS_LOAD_ERROR_INVALID_UTF8,
            "lyrics file is not valid UTF-8",
            path,
            bad_offset
        );
        return false;
    }

    start = 0;
    lyrics->had_utf8_bom = lrc_lyrics_has_utf8_bom(file_text, file_len);
    if (lyrics->had_utf8_bom) {
        start = 3;
    }

    normalized = malloc2(file_len + 1);
    normalized_len = 0;
    for (int32 i = start; i < file_len; i += 1) {
        if (file_text[i] == '\r') {
            normalized[normalized_len] = '\n';
            normalized_len += 1;
            if (((i + 1) < file_len) && (file_text[i + 1] == '\n')) {
                i += 1;
            }
        } else {
            normalized[normalized_len] = file_text[i];
            normalized_len += 1;
        }
    }
    normalized[normalized_len] = '\0';

    lyrics->text = normalized;
    lyrics->text_len = normalized_len;
    lyrics->text_cap = file_len + 1;

    return true;
}

static bool
lrc_lyrics_line_has_text(char *text, int32 text_len) {
    for (int32 i = 0; i < text_len; i += 1) {
        if ((text[i] != ' ') && (text[i] != '\t') && (text[i] != '\n')) {
            return true;
        }
    }

    return false;
}

static bool
lrc_lyrics_reserve_lines(LrcLyrics *lyrics, int32 extra) {
    int64 needed;
    int32 new_cap;

    if (extra <= 0) {
        return true;
    }

    needed = (int64)lyrics->line_count + extra;
    if (needed <= lyrics->line_cap) {
        return true;
    }
    if (needed >= MAXOF(lyrics->line_cap)) {
        return false;
    }

    new_cap = lyrics->line_cap;
    if (new_cap <= 0) {
        new_cap = 8;
    }
    while (new_cap < needed) {
        new_cap *= 2;
    }

    lyrics->lines = realloc2(lyrics->lines,
                             lyrics->line_cap,
                             new_cap,
                             SIZEOF(*lyrics->lines));
    lyrics->line_cap = new_cap;

    return true;
}

static bool
lrc_lyrics_append_line(LrcLyrics *lyrics, int32 start, int32 end) {
    LrcLyricsLine *line;

    if (!lrc_lyrics_reserve_lines(lyrics, 1)) {
        return false;
    }

    line = &lyrics->lines[lyrics->line_count];
    lyrics->line_count += 1;

    line->text = lyrics->text + start;
    line->text_len = end - start;
    line->text_start = start;
    line->text_end = end;

    if (lrc_lyrics_line_has_text(line->text, line->text_len)) {
        lyrics->nonempty_line_count += 1;
    }

    return true;
}

static bool
lrc_lyrics_split_lines(LrcLyrics *lyrics) {
    int32 line_start;

    line_start = 0;
    for (int32 i = 0; i < lyrics->text_len; i += 1) {
        if (lyrics->text[i] == '\n') {
            if (!lrc_lyrics_append_line(lyrics, line_start, i)) {
                return false;
            }
            line_start = i + 1;
        }
    }

    if ((line_start < lyrics->text_len) || (lyrics->text_len == 0)) {
        if (!lrc_lyrics_append_line(lyrics, line_start, lyrics->text_len)) {
            return false;
        }
    }

    return true;
}

static bool
lrc_lyrics_load_file(
    LrcLyrics *lyrics,
    char *path,
    LrcLyricsLoadResult *result
) {
    char *file_text;
    int32 file_len;

    if (result) {
        lrc_lyrics_load_result_init(result);
    }
    if (lyrics == NULL) {
        lrc_lyrics_load_result_set(
            result,
            LRC_LYRICS_LOAD_ERROR_INVALID_ARGUMENT,
            "lyrics object is missing",
            path,
            -1
        );
        return false;
    }
    if (lrc_lyrics_path_missing(path)) {
        lrc_lyrics_load_result_set(
            result,
            LRC_LYRICS_LOAD_ERROR_MISSING_PATH,
            "lyrics path is missing",
            path,
            -1
        );
        return false;
    }

    lrc_lyrics_destroy(lyrics);
    file_text = NULL;
    file_len = 0;
    if (!lrc_lyrics_read_file(path, &file_text, &file_len, result)) {
        return false;
    }
    if (!lrc_lyrics_normalize_text(lyrics, file_text, file_len, result, path)) {
        free2(file_text, ((int64)file_len + 1)*SIZEOF(*file_text));
        lrc_lyrics_destroy(lyrics);
        return false;
    }
    free2(file_text, ((int64)file_len + 1)*SIZEOF(*file_text));

    if (!lrc_lyrics_split_lines(lyrics)) {
        lrc_lyrics_load_result_set(
            result,
            LRC_LYRICS_LOAD_ERROR_FILE_TOO_LARGE,
            "lyrics file has too many lines",
            path,
            -1
        );
        lrc_lyrics_destroy(lyrics);
        return false;
    }
    if (lyrics->nonempty_line_count <= 0) {
        lrc_lyrics_load_result_set(
            result,
            LRC_LYRICS_LOAD_ERROR_EMPTY,
            "lyrics file is empty",
            path,
            -1
        );
        lrc_lyrics_destroy(lyrics);
        return false;
    }

    return true;
}


static void
lrc_lyrics_normalized_init(LrcLyricsNormalized *normalized) {
    if (normalized == NULL) {
        return;
    }

    memset64(normalized, 0, SIZEOF(*normalized));

    return;
}

static void
lrc_lyrics_normalized_destroy(LrcLyricsNormalized *normalized) {
    if (normalized == NULL) {
        return;
    }

    if (normalized->text) {
        free2(normalized->text,
              normalized->text_cap*SIZEOF(*normalized->text));
    }
    if (normalized->bytes) {
        free2(normalized->bytes,
              normalized->byte_cap*SIZEOF(*normalized->bytes));
    }

    lrc_lyrics_normalized_init(normalized);

    return;
}

static bool
lrc_lyrics_normalized_reserve(
    LrcLyricsNormalized *normalized,
    int32 extra_bytes
) {
    int64 needed;
    int32 new_text_cap;
    int32 new_byte_cap;

    if (extra_bytes <= 0) {
        return true;
    }

    needed = (int64)normalized->text_len + extra_bytes;
    if (needed >= MAXOF(normalized->text_cap)) {
        return false;
    }

    if ((needed + 1) > normalized->text_cap) {
        new_text_cap = normalized->text_cap;
        if (new_text_cap <= 0) {
            new_text_cap = 64;
        }
        while (new_text_cap < (needed + 1)) {
            new_text_cap *= 2;
        }

        normalized->text = realloc2(normalized->text,
                                    normalized->text_cap,
                                    new_text_cap,
                                    SIZEOF(*normalized->text));
        normalized->text_cap = new_text_cap;
    }

    if (needed > normalized->byte_cap) {
        new_byte_cap = normalized->byte_cap;
        if (new_byte_cap <= 0) {
            new_byte_cap = 64;
        }
        while (new_byte_cap < needed) {
            new_byte_cap *= 2;
        }

        normalized->bytes = realloc2(normalized->bytes,
                                     normalized->byte_cap,
                                     new_byte_cap,
                                     SIZEOF(*normalized->bytes));
        normalized->byte_cap = new_byte_cap;
    }

    return true;
}

static bool
lrc_lyrics_normalized_append_bytes(
    LrcLyricsNormalized *normalized,
    char *bytes,
    int32 bytes_len,
    int32 line_index,
    int32 source_start,
    int32 source_end
) {
    LrcLyricsNormalizedByte *map;

    if (!lrc_lyrics_normalized_reserve(normalized, bytes_len)) {
        return false;
    }

    memcpy64(normalized->text + normalized->text_len, bytes, bytes_len);
    for (int32 i = 0; i < bytes_len; i += 1) {
        map = &normalized->bytes[normalized->byte_count];
        normalized->byte_count += 1;

        map->line_index = line_index;
        map->source_start = source_start;
        map->source_end = source_end;
    }
    normalized->text_len += bytes_len;
    normalized->text[normalized->text_len] = '\0';

    return true;
}

static bool
lrc_lyrics_normalized_append_space(
    LrcLyricsNormalized *normalized,
    int32 line_index,
    int32 source_start,
    int32 source_end
) {
    char space;

    if (normalized->text_len <= 0) {
        return true;
    }
    if (normalized->text[normalized->text_len - 1] == ' ') {
        return true;
    }

    space = ' ';
    return lrc_lyrics_normalized_append_bytes(normalized,
                                              &space,
                                              1,
                                              line_index,
                                              source_start,
                                              source_end);
}

static bool
lrc_lyrics_normalized_append_char(
    LrcLyricsNormalized *normalized,
    char c,
    int32 line_index,
    int32 source_start,
    int32 source_end
) {
    return lrc_lyrics_normalized_append_bytes(normalized,
                                              &c,
                                              1,
                                              line_index,
                                              source_start,
                                              source_end);
}

static bool
lrc_lyrics_ascii_space(char c) {
    if (c == ' ') {
        return true;
    }
    if (c == '\t') {
        return true;
    }
    if (c == '\n') {
        return true;
    }
    if (c == '\v') {
        return true;
    }
    if (c == '\f') {
        return true;
    }

    return false;
}

static int32
lrc_lyrics_line_trim_start(LrcLyricsLine *line) {
    int32 start;

    start = line->text_start;
    while ((start < line->text_end)
           && lrc_lyrics_ascii_space(line->text[start - line->text_start])) {
        start += 1;
    }

    return start;
}

static int32
lrc_lyrics_line_trim_end(LrcLyricsLine *line, int32 start) {
    int32 end;

    end = line->text_end;
    while ((end > start)
           && lrc_lyrics_ascii_space(line->text[end - line->text_start - 1])) {
        end -= 1;
    }

    return end;
}

static bool
lrc_lyrics_line_is_section_marker(
    LrcLyrics *lyrics,
    int32 start,
    int32 end
) {
    char first;
    char last;

    if ((end - start) < 2) {
        return false;
    }

    first = lyrics->text[start];
    last = lyrics->text[end - 1];
    if (((first == '[') || (first == '('))
        && ((last == ']') || (last == ')'))) {
        return true;
    }

    return false;
}

static bool
lrc_lyrics_ascii_alnum(char c) {
    return isalnum((uint8)c);
}

static char
lrc_lyrics_ascii_lower(char c) {
    if ((c >= 'A') && (c <= 'Z')) {
        return (char)(c - 'A' + 'a');
    }

    return c;
}

static bool
lrc_lyrics_normalize_line(
    LrcLyrics *lyrics,
    LrcLyricsNormalized *normalized,
    int32 line_index,
    int32 start,
    int32 end
) {
    bool wrote_line;
    int32 original_len;

    wrote_line = false;
    original_len = normalized->text_len;
    for (int32 i = start; i < end;) {
        uint32 rune;
        int32 step;

        step = utf8_decode_raw(lyrics->text + i, &rune, end - i);
        if (step <= 0) {
            return false;
        }

        if (rune < 0x80) {
            char c;

            c = lyrics->text[i];
            if (lrc_lyrics_ascii_alnum(c)) {
                c = lrc_lyrics_ascii_lower(c);
                if (!wrote_line && (normalized->text_len > 0)) {
                    if (!lrc_lyrics_normalized_append_space(normalized,
                                                            line_index,
                                                            i,
                                                            i + step)) {
                        return false;
                    }
                }
                if (!lrc_lyrics_normalized_append_char(normalized,
                                                       c,
                                                       line_index,
                                                       i,
                                                       i + step)) {
                    return false;
                }
                wrote_line = true;
            } else if (lrc_lyrics_ascii_space(c)) {
                if (wrote_line) {
                    if (!lrc_lyrics_normalized_append_space(normalized,
                                                            line_index,
                                                            i,
                                                            i + step)) {
                        return false;
                    }
                }
            }
        } else {
            if (!wrote_line && (normalized->text_len > 0)) {
                if (!lrc_lyrics_normalized_append_space(normalized,
                                                        line_index,
                                                        i,
                                                        i + step)) {
                    return false;
                }
            }
            if (!lrc_lyrics_normalized_append_bytes(normalized,
                                                    lyrics->text + i,
                                                    step,
                                                    line_index,
                                                    i,
                                                    i + step)) {
                return false;
            }
            wrote_line = true;
        }

        i += step;
    }

    if ((normalized->text_len > 0)
        && (normalized->text[normalized->text_len - 1] == ' ')) {
        normalized->text_len -= 1;
        normalized->byte_count -= 1;
        normalized->text[normalized->text_len] = '\0';
    }

    if (normalized->text_len > original_len) {
        normalized->alignable_line_count += 1;
    }

    return true;
}

static bool
lrc_lyrics_normalize(
    LrcLyrics *lyrics,
    LrcLyricsNormalized *normalized
) {
    if ((lyrics == NULL) || (normalized == NULL)) {
        return false;
    }

    lrc_lyrics_normalized_destroy(normalized);
    for (int32 i = 0; i < lyrics->line_count; i += 1) {
        LrcLyricsLine *line;
        int32 start;
        int32 end;

        line = &lyrics->lines[i];
        start = lrc_lyrics_line_trim_start(line);
        end = lrc_lyrics_line_trim_end(line, start);
        if (start >= end) {
            continue;
        }
        if (lrc_lyrics_line_is_section_marker(lyrics, start, end)) {
            continue;
        }
        if (!lrc_lyrics_normalize_line(lyrics,
                                       normalized,
                                       i,
                                       start,
                                       end)) {
            lrc_lyrics_normalized_destroy(normalized);
            return false;
        }
    }

    return normalized->text_len > 0;
}

static int32
lrc_lyrics_normalized_line_at(
    LrcLyricsNormalized *normalized,
    int32 byte_offset
) {
    if (normalized == NULL) {
        return -1;
    }
    if ((byte_offset < 0) || (byte_offset >= normalized->byte_count)) {
        return -1;
    }

    return normalized->bytes[byte_offset].line_index;
}

#if TESTING_lyrics

#define CBASE_IMPLEMENT
#include "cbase.h"

static int32
lyrics_test_fail(char *name) {
    error2("lyrics test failed: %s\n", name);

    return 1;
}

static bool
lyrics_test_write(char *path, char *text, int32 text_len) {
    return write_entire_file(path, text, text_len);
}

static bool
lyrics_test_load_text(LrcLyrics *lyrics, char *text, int32 text_len) {
    LrcLyricsLoadResult result;
    char temp_dir[PATH_MAX];
    char path[PATH_MAX];
    int32 len;
    bool ok;

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "lyrics_text");
    len = snprintf2(path, SIZEOF(path), "%s/lyrics.txt", temp_dir);
    if ((len <= 0) || (len >= SIZEOF(path))) {
        test_remove_tree(temp_dir);
        return false;
    }
    if (!lyrics_test_write(path, text, text_len)) {
        test_remove_tree(temp_dir);
        return false;
    }

    lrc_lyrics_init(lyrics);
    ok = lrc_lyrics_load_file(lyrics, path, &result);
    test_remove_tree(temp_dir);

    return ok;
}

static int32
lyrics_test_crlf_and_trailing_newline(void) {
    LrcLyrics lyrics;
    char text[] = "First\r\nSecond\rThird\n";

    if (!lyrics_test_load_text(&lyrics, text, strlen32(text))) {
        return lyrics_test_fail("load crlf text");
    }
    ASSERT(strequal2(lyrics.text, lyrics.text_len,
                     "First\nSecond\nThird\n", 19));
    ASSERT(lyrics.line_count == 3);
    ASSERT(lyrics.nonempty_line_count == 3);
    ASSERT(strequal2(lyrics.lines[0].text, lyrics.lines[0].text_len,
                     "First", 5));
    ASSERT(strequal2(lyrics.lines[1].text, lyrics.lines[1].text_len,
                     "Second", 6));
    ASSERT(strequal2(lyrics.lines[2].text, lyrics.lines[2].text_len,
                     "Third", 5));

    lrc_lyrics_destroy(&lyrics);

    return 0;
}

static int32
lyrics_test_bom_unicode_and_blank_lines(void) {
    LrcLyrics lyrics;
    char text[] = "\xEF\xBB\xBFOlá\r\n\r\n世界";

    if (!lyrics_test_load_text(&lyrics, text, SIZEOF(text) - 1)) {
        return lyrics_test_fail("load bom unicode text");
    }
    ASSERT(lyrics.had_utf8_bom);
    ASSERT(strequal2(lyrics.text, lyrics.text_len, "Olá\n\n世界", 12));
    ASSERT(lyrics.line_count == 3);
    ASSERT(lyrics.nonempty_line_count == 2);
    ASSERT(strequal2(lyrics.lines[0].text, lyrics.lines[0].text_len,
                     "Olá", 4));
    ASSERT(lyrics.lines[1].text_len == 0);
    ASSERT(strequal2(lyrics.lines[2].text, lyrics.lines[2].text_len,
                     "世界", 6));

    lrc_lyrics_destroy(&lyrics);

    return 0;
}

static int32
lyrics_test_reject_empty(void) {
    LrcLyrics lyrics;
    LrcLyricsLoadResult result;
    char temp_dir[PATH_MAX];
    char path[PATH_MAX];
    char text[] = "\r\n\n\t \n";
    int32 len;

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "lyrics_empty");
    len = snprintf2(path, SIZEOF(path), "%s/lyrics.txt", temp_dir);
    if ((len <= 0) || (len >= SIZEOF(path))) {
        test_remove_tree(temp_dir);
        return lyrics_test_fail("empty path");
    }
    if (!lyrics_test_write(path, text, strlen32(text))) {
        test_remove_tree(temp_dir);
        return lyrics_test_fail("write empty text");
    }

    lrc_lyrics_init(&lyrics);
    if (lrc_lyrics_load_file(&lyrics, path, &result)) {
        lrc_lyrics_destroy(&lyrics);
        test_remove_tree(temp_dir);
        return lyrics_test_fail("empty text accepted");
    }
    ASSERT(result.error == LRC_LYRICS_LOAD_ERROR_EMPTY);
    ASSERT(lyrics.text == NULL);
    ASSERT(lyrics.line_count == 0);

    test_remove_tree(temp_dir);

    return 0;
}

static int32
lyrics_test_reject_invalid_utf8(void) {
    LrcLyrics lyrics;
    LrcLyricsLoadResult result;
    char temp_dir[PATH_MAX];
    char path[PATH_MAX];
    char text[] = {'o', 'k', '\n', (char)0xC0, '\0'};
    int32 len;

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), "lyrics_invalid_utf8");
    len = snprintf2(path, SIZEOF(path), "%s/lyrics.txt", temp_dir);
    if ((len <= 0) || (len >= SIZEOF(path))) {
        test_remove_tree(temp_dir);
        return lyrics_test_fail("invalid utf8 path");
    }
    if (!lyrics_test_write(path, text, 4)) {
        test_remove_tree(temp_dir);
        return lyrics_test_fail("write invalid utf8 text");
    }

    lrc_lyrics_init(&lyrics);
    if (lrc_lyrics_load_file(&lyrics, path, &result)) {
        lrc_lyrics_destroy(&lyrics);
        test_remove_tree(temp_dir);
        return lyrics_test_fail("invalid utf8 accepted");
    }
    ASSERT(result.error == LRC_LYRICS_LOAD_ERROR_INVALID_UTF8);
    ASSERT(result.byte_offset == 3);

    test_remove_tree(temp_dir);

    return 0;
}


static int32
lyrics_test_normalize_punctuation_sections_and_mapping(void) {
    LrcLyrics lyrics;
    LrcLyricsNormalized normalized;
    char text[] = "  Hello, WORLD!!\n[Chorus]\nBang-bang   MAXWELL's\n";
    int32 bang_offset;
    char *bang;

    if (!lyrics_test_load_text(&lyrics, text, strlen32(text))) {
        return lyrics_test_fail("load normalization text");
    }

    lrc_lyrics_normalized_init(&normalized);
    if (!lrc_lyrics_normalize(&lyrics, &normalized)) {
        lrc_lyrics_destroy(&lyrics);
        return lyrics_test_fail("normalize punctuation text");
    }

    ASSERT(strequal2(normalized.text, normalized.text_len,
                     "hello world bangbang maxwells", 29));
    ASSERT(normalized.byte_count == normalized.text_len);
    ASSERT(normalized.alignable_line_count == 2);
    ASSERT(lrc_lyrics_normalized_line_at(&normalized, 0) == 0);
    ASSERT(lrc_lyrics_normalized_line_at(&normalized, 10) == 0);

    bang = memmem64(normalized.text,
                    normalized.text_len,
                    STRLIT("bangbang"));
    ASSERT(bang != NULL);
    bang_offset = (int32)(bang - normalized.text);
    ASSERT(lrc_lyrics_normalized_line_at(&normalized, bang_offset) == 2);
    ASSERT(lrc_lyrics_normalized_line_at(&normalized,
                                         normalized.text_len - 1) == 2);

    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);

    return 0;
}

static int32
lyrics_test_normalize_unicode_and_blank_lines(void) {
    LrcLyrics lyrics;
    LrcLyricsNormalized normalized;
    char text[] = "Olá,   世界!\n\n(Bridge)\nAgain\n";
    char *again;
    int32 again_offset;

    if (!lyrics_test_load_text(&lyrics, text, strlen32(text))) {
        return lyrics_test_fail("load unicode normalization text");
    }

    lrc_lyrics_normalized_init(&normalized);
    if (!lrc_lyrics_normalize(&lyrics, &normalized)) {
        lrc_lyrics_destroy(&lyrics);
        return lyrics_test_fail("normalize unicode text");
    }

    ASSERT(strequal2(normalized.text, normalized.text_len,
                     "olá 世界 again", 17));
    ASSERT(normalized.alignable_line_count == 2);
    ASSERT(lrc_lyrics_normalized_line_at(&normalized, 0) == 0);
    ASSERT(lrc_lyrics_normalized_line_at(&normalized, 3) == 0);

    again = memmem64(normalized.text,
                     normalized.text_len,
                     STRLIT("again"));
    ASSERT(again != NULL);
    again_offset = (int32)(again - normalized.text);
    ASSERT(lrc_lyrics_normalized_line_at(&normalized, again_offset) == 3);

    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);

    return 0;
}

static int32
lyrics_test_optional_maxwell_txt(void) {
    LrcLyrics lyrics;
    LrcLyricsLoadResult result;
    char *path;

    path = getenv("LRC_TEST_MAXWELL_TXT");
    if (path == NULL) {
        path = "next-phase/maxwell.txt";
    }
    if (!util_file_exists(path)) {
        return 0;
    }

    lrc_lyrics_init(&lyrics);
    if (!lrc_lyrics_load_file(&lyrics, path, &result)) {
        return lyrics_test_fail("load maxwell lyrics");
    }

    ASSERT(lyrics.line_count == 6);
    ASSERT(lyrics.nonempty_line_count == 5);
    ASSERT(strequal2(lyrics.lines[0].text, lyrics.lines[0].text_len,
                     "Can I take you out to the pictures, Joan?", 41));
    ASSERT(lyrics.lines[3].text_len == 0);
    ASSERT(strequal2(lyrics.lines[4].text, lyrics.lines[4].text_len,
                     "Bang, bang, Maxwell's silver hammer", 35));
    ASSERT(strequal2(lyrics.lines[5].text, lyrics.lines[5].text_len,
                     "Came down upon her head", 23));

    {
        LrcLyricsNormalized normalized;
        char *bang;
        int32 bang_offset;

        lrc_lyrics_normalized_init(&normalized);
        if (!lrc_lyrics_normalize(&lyrics, &normalized)) {
            lrc_lyrics_destroy(&lyrics);
            return lyrics_test_fail("normalize maxwell lyrics");
        }

        ASSERT(strequal2(
            normalized.text,
            normalized.text_len,
            "can i take you out to the pictures joan "
            "but as shes getting ready to go "
            "a knock comes on the door "
            "bang bang maxwells silver hammer "
            "came down upon her head",
            154
        ));
        ASSERT(normalized.alignable_line_count == 5);
        ASSERT(lrc_lyrics_normalized_line_at(&normalized, 0) == 0);

        bang = memmem64(normalized.text,
                        normalized.text_len,
                        STRLIT("bang bang"));
        ASSERT(bang != NULL);
        bang_offset = (int32)(bang - normalized.text);
        ASSERT(lrc_lyrics_normalized_line_at(&normalized, bang_offset) == 4);

        lrc_lyrics_normalized_destroy(&normalized);
    }

    lrc_lyrics_destroy(&lyrics);

    return 0;
}

int32
main(void) {
    if (lyrics_test_crlf_and_trailing_newline() != 0) {
        exit(1);
    }
    if (lyrics_test_bom_unicode_and_blank_lines() != 0) {
        exit(1);
    }
    if (lyrics_test_reject_empty() != 0) {
        exit(1);
    }
    if (lyrics_test_reject_invalid_utf8() != 0) {
        exit(1);
    }
    if (lyrics_test_normalize_punctuation_sections_and_mapping() != 0) {
        exit(1);
    }
    if (lyrics_test_normalize_unicode_and_blank_lines() != 0) {
        exit(1);
    }
    if (lyrics_test_optional_maxwell_txt() != 0) {
        exit(1);
    }

    return 0;
}

#endif /* TESTING_lyrics */
