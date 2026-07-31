#include "ctc_text.h"
#include "lyrics.h"

#include "cbase.h"

#if !defined(TESTING_ctc_text)
#define TESTING_ctc_text 0
#endif

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
    if (normalized->lines) {
        free2(normalized->lines,
              normalized->line_cap*SIZEOF(*normalized->lines));
    }

    lrc_lyrics_normalized_init(normalized);

    return;
}

static bool
lrc_lyrics_normalized_alloc_lines(
    LrcLyricsNormalized *normalized,
    int32 line_count
) {
    if (line_count <= 0) {
        return true;
    }

    normalized->lines = malloc2(line_count*SIZEOF(*normalized->lines));
    normalized->line_count = line_count;
    normalized->line_cap = line_count;

    for (int32 i = 0; i < line_count; i += 1) {
        normalized->lines[i].kind = LRC_LYRICS_NORMALIZED_LINE_KIND_BLANK;
        normalized->lines[i].normalized_start = -1;
        normalized->lines[i].normalized_end = -1;
    }

    return true;
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
    LrcLyricsNormalizedLine *line_range;
    bool wrote_line;

    line_range = &normalized->lines[line_index];
    wrote_line = false;
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
                if (line_range->normalized_start < 0) {
                    line_range->normalized_start = normalized->text_len;
                }
                if (!lrc_lyrics_normalized_append_char(normalized,
                                                       c,
                                                       line_index,
                                                       i,
                                                       i + step)) {
                    return false;
                }
                line_range->normalized_end = normalized->text_len;
                wrote_line = true;
            } else if (lrc_lyrics_ascii_space(c)) {
                if (wrote_line) {
                    if (!lrc_lyrics_normalized_append_space(normalized,
                                                            line_index,
                                                            i,
                                                            i + step)) {
                        return false;
                    }
                    line_range->normalized_end = normalized->text_len;
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
            if (line_range->normalized_start < 0) {
                line_range->normalized_start = normalized->text_len;
            }
            if (!lrc_lyrics_normalized_append_bytes(normalized,
                                                    lyrics->text + i,
                                                    step,
                                                    line_index,
                                                    i,
                                                    i + step)) {
                return false;
            }
            line_range->normalized_end = normalized->text_len;
            wrote_line = true;
        }

        i += step;
    }

    if ((normalized->text_len > 0)
        && (normalized->text[normalized->text_len - 1] == ' ')) {
        normalized->text_len -= 1;
        normalized->byte_count -= 1;
        normalized->text[normalized->text_len] = '\0';
        line_range->normalized_end = normalized->text_len;
    }

    if ((line_range->normalized_start >= 0)
        && (line_range->normalized_end > line_range->normalized_start)) {
        line_range->kind = LRC_LYRICS_NORMALIZED_LINE_KIND_ALIGNABLE;
        normalized->alignable_line_count += 1;
    } else {
        line_range->kind = LRC_LYRICS_NORMALIZED_LINE_KIND_PUNCTUATION_ONLY;
        line_range->normalized_start = -1;
        line_range->normalized_end = -1;
    }

    return true;
}

static bool
lrc_lyrics_normalize_with_options(
    LrcLyrics *lyrics,
    LrcLyricsNormalized *normalized,
    LrcLyricsPreprocessOptions *options
) {
    if ((lyrics == NULL) || (normalized == NULL)) {
        return false;
    }

    (void)options;

    lrc_lyrics_normalized_destroy(normalized);
    if (!lrc_lyrics_normalized_alloc_lines(normalized, lyrics->line_count)) {
        return false;
    }

    for (int32 i = 0; i < lyrics->line_count; i += 1) {
        LrcLyricsLine *line;
        int32 start;
        int32 end;

        line = &lyrics->lines[i];
        start = lrc_lyrics_line_trim_start(line);
        end = lrc_lyrics_line_trim_end(line, start);
        if (start >= end) {
            normalized->lines[i].kind = LRC_LYRICS_NORMALIZED_LINE_KIND_BLANK;
            continue;
        }
        if (lrc_lyrics_line_is_section_marker(lyrics, start, end)) {
            normalized->lines[i].kind =
                LRC_LYRICS_NORMALIZED_LINE_KIND_SECTION_MARKER;
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

static bool
lrc_lyrics_normalize(
    LrcLyrics *lyrics,
    LrcLyricsNormalized *normalized
) {
    LrcLyricsPreprocessOptions options;

    lrc_lyrics_preprocess_options_init(&options);

    return lrc_lyrics_normalize_with_options(lyrics, normalized, &options);
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

static enum LrcLyricsNormalizedLineKind
lrc_lyrics_normalized_line_kind(
    LrcLyricsNormalized *normalized,
    int32 line_index
) {
    if (normalized == NULL) {
        return LRC_LYRICS_NORMALIZED_LINE_KIND_BLANK;
    }
    if ((line_index < 0) || (line_index >= normalized->line_count)) {
        return LRC_LYRICS_NORMALIZED_LINE_KIND_BLANK;
    }

    return normalized->lines[line_index].kind;
}

static bool
lrc_lyrics_normalized_line_range(
    LrcLyricsNormalized *normalized,
    int32 line_index,
    int32 *start,
    int32 *end
) {
    LrcLyricsNormalizedLine *line;

    if (start) {
        *start = -1;
    }
    if (end) {
        *end = -1;
    }
    if (normalized == NULL) {
        return false;
    }
    if ((line_index < 0) || (line_index >= normalized->line_count)) {
        return false;
    }

    line = &normalized->lines[line_index];
    if (line->kind != LRC_LYRICS_NORMALIZED_LINE_KIND_ALIGNABLE) {
        return false;
    }
    if (start) {
        *start = line->normalized_start;
    }
    if (end) {
        *end = line->normalized_end;
    }

    return true;
}


#if TESTING_ctc_text

#define CBASE_IMPLEMENT
#include "cbase.h"
#include "lyrics.c"

static int32
ctc_text_test_fail(char *name) {
    error2("CTC text test failed: %s\n", name);

    return 1;
}

static void
ctc_text_join_path(
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
ctc_text_load_lyrics(
    LrcLyrics *lyrics,
    char *text,
    char *name
) {
    LrcLyricsLoadResult result;
    char temp_dir[PATH_MAX];
    char path[PATH_MAX];
    bool ok;

    test_make_temp_dir(temp_dir, SIZEOF(temp_dir), name);
    ctc_text_join_path(path, SIZEOF(path), temp_dir, "lyrics.txt");
    if (!write_entire_file(path, text, strlen32(text))) {
        test_remove_tree(temp_dir);
        return false;
    }

    lrc_lyrics_init(lyrics);
    ok = lrc_lyrics_load_file(lyrics, path, &result);
    test_remove_tree(temp_dir);

    return ok;
}

static int32
ctc_text_test_default_options(void) {
    LrcLyricsPreprocessOptions options;

    lrc_lyrics_preprocess_options_init(&options);

    ASSERT(options.split_size == LRC_LYRICS_PREPROCESS_SPLIT_SIZE_CURRENT);
    ASSERT(
        options.star_frequency == LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_EDGES
    );
    ASSERT(options.romanization == LRC_LYRICS_PREPROCESS_ROMANIZATION_OFF);

    return 0;
}

static int32
ctc_text_test_current_normalization_mapping(void) {
    LrcLyrics lyrics;
    LrcLyricsNormalized normalized;

    if (!ctc_text_load_lyrics(&lyrics,
                              "Hello, WORLD!\n[Verse]\nagain?!\n",
                              "ctc_text_mapping")) {
        return ctc_text_test_fail("load lyrics");
    }

    lrc_lyrics_normalized_init(&normalized);
    if (!lrc_lyrics_normalize(&lyrics, &normalized)) {
        lrc_lyrics_destroy(&lyrics);
        return ctc_text_test_fail("normalize lyrics");
    }

    ASSERT(strequal2(normalized.text,
                     normalized.text_len,
                     STRLIT("hello world again")));
    ASSERT(normalized.alignable_line_count == 2);
    ASSERT(lrc_lyrics_normalized_line_kind(
        &normalized,
        1
    ) == LRC_LYRICS_NORMALIZED_LINE_KIND_SECTION_MARKER);
    ASSERT(lrc_lyrics_normalized_line_at(&normalized, 0) == 0);
    ASSERT(lrc_lyrics_normalized_line_at(&normalized,
                                         normalized.text_len - 1) == 2);

    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);

    return 0;
}

int
main(void) {
    int32 status;

    status = 0;

    status += ctc_text_test_default_options();
    status += ctc_text_test_current_normalization_mapping();

    return status;
}
#endif /* TESTING_ctc_text */
