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
    if (normalized->segments) {
        free2(normalized->segments,
              normalized->segment_cap*SIZEOF(*normalized->segments));
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
lrc_lyrics_normalized_reserve_segments(
    LrcLyricsNormalized *normalized,
    int32 extra_segments
) {
    int64 needed;
    int32 new_cap;

    if (extra_segments <= 0) {
        return true;
    }

    needed = (int64)normalized->segment_count + extra_segments;
    if (needed <= normalized->segment_cap) {
        return true;
    }
    if (needed >= MAXOF(normalized->segment_cap)) {
        return false;
    }

    new_cap = normalized->segment_cap;
    if (new_cap <= 0) {
        new_cap = 32;
    }
    while (new_cap < needed) {
        new_cap *= 2;
    }

    normalized->segments = realloc2(normalized->segments,
                                    normalized->segment_cap,
                                    new_cap,
                                    SIZEOF(*normalized->segments));
    normalized->segment_cap = new_cap;

    return true;
}

static bool
lrc_lyrics_normalized_append_segment(
    LrcLyricsNormalized *normalized,
    int32 line_index,
    int32 source_start,
    int32 source_end,
    int32 normalized_start,
    int32 normalized_end
) {
    CtcTextSegment *segment;

    if (source_end <= source_start) {
        return true;
    }
    if (normalized_end <= normalized_start) {
        return true;
    }
    if (!lrc_lyrics_normalized_reserve_segments(normalized, 1)) {
        return false;
    }

    segment = &normalized->segments[normalized->segment_count];
    normalized->segment_count += 1;

    segment->line_index = line_index;
    segment->source_start = source_start;
    segment->source_end = source_end;
    segment->normalized_start = normalized_start;
    segment->normalized_end = normalized_end;
    segment->target_start = normalized_start;
    segment->target_end = normalized_end;

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


typedef struct CtcTextSegmentBuild {
    bool active;
    bool has_text;

    int32 line_index;
    int32 source_start;
    int32 source_end;
    int32 normalized_start;
    int32 normalized_end;
} CtcTextSegmentBuild;

static void
ctc_text_segment_build_init(
    CtcTextSegmentBuild *segment
) {
    memset64(segment, 0, SIZEOF(*segment));

    segment->line_index = -1;
    segment->source_start = -1;
    segment->source_end = -1;
    segment->normalized_start = -1;
    segment->normalized_end = -1;

    return;
}

static void
ctc_text_segment_build_source(
    CtcTextSegmentBuild *segment,
    int32 line_index,
    int32 source_start,
    int32 source_end
) {
    if (!segment->active) {
        segment->active = true;
        segment->line_index = line_index;
        segment->source_start = source_start;
    }

    segment->source_end = source_end;

    return;
}

static void
ctc_text_segment_build_text(
    CtcTextSegmentBuild *segment,
    int32 normalized_start,
    int32 normalized_end
) {
    ASSERT(segment->active);

    if (!segment->has_text) {
        segment->has_text = true;
        segment->normalized_start = normalized_start;
    }

    segment->normalized_end = normalized_end;

    return;
}

static bool
ctc_text_segment_build_finish(
    LrcLyricsNormalized *normalized,
    CtcTextSegmentBuild *segment
) {
    bool ok;

    if (!segment->active) {
        return true;
    }
    if (!segment->has_text) {
        ctc_text_segment_build_init(segment);
        return true;
    }

    ok = lrc_lyrics_normalized_append_segment(normalized,
                                              segment->line_index,
                                              segment->source_start,
                                              segment->source_end,
                                              segment->normalized_start,
                                              segment->normalized_end);
    ctc_text_segment_build_init(segment);

    return ok;
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
    CtcTextSegmentBuild segment;
    bool wrote_line;

    line_range = &normalized->lines[line_index];
    ctc_text_segment_build_init(&segment);
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
            if (!lrc_lyrics_ascii_space(c)) {
                ctc_text_segment_build_source(&segment,
                                              line_index,
                                              i,
                                              i + step);
            }
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
                ctc_text_segment_build_text(&segment,
                                             normalized->text_len,
                                             normalized->text_len);
                if (!lrc_lyrics_normalized_append_char(normalized,
                                                       c,
                                                       line_index,
                                                       i,
                                                       i + step)) {
                    return false;
                }
                ctc_text_segment_build_text(&segment,
                                             segment.normalized_start,
                                             normalized->text_len);
                line_range->normalized_end = normalized->text_len;
                wrote_line = true;
            } else if (lrc_lyrics_ascii_space(c)) {
                if (wrote_line) {
                    if (!ctc_text_segment_build_finish(normalized, &segment)) {
                        return false;
                    }
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
            ctc_text_segment_build_source(&segment,
                                          line_index,
                                          i,
                                          i + step);
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
            ctc_text_segment_build_text(&segment,
                                         normalized->text_len,
                                         normalized->text_len);
            if (!lrc_lyrics_normalized_append_bytes(normalized,
                                                    lyrics->text + i,
                                                    step,
                                                    line_index,
                                                    i,
                                                    i + step)) {
                return false;
            }
            ctc_text_segment_build_text(&segment,
                                         segment.normalized_start,
                                         normalized->text_len);
            line_range->normalized_end = normalized->text_len;
            wrote_line = true;
        }

        i += step;
    }

    if (!ctc_text_segment_build_finish(normalized, &segment)) {
        return false;
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
lrc_lyrics_normalize_word_segment(
    LrcLyrics *lyrics,
    LrcLyricsNormalized *normalized,
    LrcLyricsNormalizedLine *line_range,
    int32 line_index,
    int32 word_start,
    int32 word_end,
    bool *wrote_line
) {
    int32 normalized_start;
    int32 normalized_end;
    bool wrote_word;

    normalized_start = -1;
    normalized_end = -1;
    wrote_word = false;

    for (int32 i = word_start; i < word_end;) {
        uint32 rune;
        int32 step;

        step = utf8_decode_raw(lyrics->text + i, &rune, word_end - i);
        if (step <= 0) {
            return false;
        }

        if (rune < 0x80) {
            char c;

            c = lyrics->text[i];
            if (lrc_lyrics_ascii_alnum(c)) {
                c = lrc_lyrics_ascii_lower(c);
                if (!wrote_word) {
                    if ((normalized->text_len > 0)
                        && !lrc_lyrics_normalized_append_space(
                            normalized,
                            line_index,
                            word_start,
                            word_end
                        )) {
                        return false;
                    }
                    if (line_range->normalized_start < 0) {
                        line_range->normalized_start = normalized->text_len;
                    }
                    normalized_start = normalized->text_len;
                    wrote_word = true;
                }
                if (!lrc_lyrics_normalized_append_char(normalized,
                                                       c,
                                                       line_index,
                                                       i,
                                                       i + step)) {
                    return false;
                }
                normalized_end = normalized->text_len;
            }
        } else {
            if (!wrote_word) {
                if ((normalized->text_len > 0)
                    && !lrc_lyrics_normalized_append_space(normalized,
                                                           line_index,
                                                           word_start,
                                                           word_end)) {
                    return false;
                }
                if (line_range->normalized_start < 0) {
                    line_range->normalized_start = normalized->text_len;
                }
                normalized_start = normalized->text_len;
                wrote_word = true;
            }
            if (!lrc_lyrics_normalized_append_bytes(normalized,
                                                    lyrics->text + i,
                                                    step,
                                                    line_index,
                                                    i,
                                                    i + step)) {
                return false;
            }
            normalized_end = normalized->text_len;
        }

        i += step;
    }

    if (!wrote_word) {
        return true;
    }

    line_range->normalized_end = normalized_end;
    *wrote_line = true;
    return lrc_lyrics_normalized_append_segment(normalized,
                                                line_index,
                                                word_start,
                                                word_end,
                                                normalized_start,
                                                normalized_end);
}

static int32
lrc_lyrics_next_word_end(
    LrcLyrics *lyrics,
    int32 start,
    int32 end
) {
    for (int32 i = start; i < end;) {
        uint32 rune;
        int32 step;

        step = utf8_decode_raw(lyrics->text + i, &rune, end - i);
        if (step <= 0) {
            return -1;
        }
        if (rune < 0x80) {
            char c;

            c = lyrics->text[i];
            if (lrc_lyrics_ascii_space(c)) {
                return i;
            }
        }

        i += step;
    }

    return end;
}

static bool
lrc_lyrics_normalize_line_word(
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
        int32 word_start;
        int32 word_end;

        step = utf8_decode_raw(lyrics->text + i, &rune, end - i);
        if (step <= 0) {
            return false;
        }
        if ((rune < 0x80) && lrc_lyrics_ascii_space(lyrics->text[i])) {
            i += step;
            continue;
        }

        word_start = i;
        word_end = lrc_lyrics_next_word_end(lyrics, word_start, end);
        if (word_end < 0) {
            return false;
        }
        if (!lrc_lyrics_normalize_word_segment(lyrics,
                                               normalized,
                                               line_range,
                                               line_index,
                                               word_start,
                                               word_end,
                                               &wrote_line)) {
            return false;
        }
        i = word_end;
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
    LrcLyricsPreprocessOptions local_options;

    if ((lyrics == NULL) || (normalized == NULL)) {
        return false;
    }

    if (options == NULL) {
        lrc_lyrics_preprocess_options_init(&local_options);
        options = &local_options;
    }

    lrc_lyrics_normalized_destroy(normalized);
    if (!lrc_lyrics_normalized_alloc_lines(normalized, lyrics->line_count)) {
        return false;
    }

    for (int32 i = 0; i < lyrics->line_count; i += 1) {
        LrcLyricsLine *line;
        bool ok;
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

        switch (options->split_size) {
        case LRC_LYRICS_PREPROCESS_SPLIT_SIZE_CURRENT:
            ok = lrc_lyrics_normalize_line(lyrics,
                                           normalized,
                                           i,
                                           start,
                                           end);
            break;
        case LRC_LYRICS_PREPROCESS_SPLIT_SIZE_WORD:
            ok = lrc_lyrics_normalize_line_word(lyrics,
                                                normalized,
                                                i,
                                                start,
                                                end);
            break;
        case LRC_LYRICS_PREPROCESS_SPLIT_SIZE_CHAR:
        case LRC_LYRICS_PREPROCESS_SPLIT_SIZE_SENTENCE:
        default:
            ok = false;
            break;
        }
        if (!ok) {
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

static int32
lrc_lyrics_normalized_segment_count(
    LrcLyricsNormalized *normalized
) {
    if (normalized == NULL) {
        return 0;
    }

    return normalized->segment_count;
}

static CtcTextSegment *
lrc_lyrics_normalized_segment(
    LrcLyricsNormalized *normalized,
    int32 segment_index
) {
    if (normalized == NULL) {
        return NULL;
    }
    if ((segment_index < 0) || (segment_index >= normalized->segment_count)) {
        return NULL;
    }

    return &normalized->segments[segment_index];
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


static void
ctc_text_test_assert_segment(
    LrcLyrics *lyrics,
    LrcLyricsNormalized *normalized,
    int32 segment_index,
    int32 expected_line_index,
    int32 expected_normalized_start,
    int32 expected_normalized_end,
    char *expected_source
) {
    CtcTextSegment *segment;
    int32 source_len;
    int32 normalized_len;
    int32 target_len;

    segment = lrc_lyrics_normalized_segment(normalized, segment_index);
    ASSERT(segment);

    source_len = segment->source_end - segment->source_start;
    normalized_len = segment->normalized_end - segment->normalized_start;
    target_len = segment->target_end - segment->target_start;

    ASSERT(segment->line_index == expected_line_index);
    ASSERT(segment->normalized_start == expected_normalized_start);
    ASSERT(segment->normalized_end == expected_normalized_end);
    ASSERT(segment->target_start == expected_normalized_start);
    ASSERT(segment->target_end == expected_normalized_end);
    ASSERT(strequal2(lyrics->text + segment->source_start,
                     source_len,
                     expected_source,
                     strlen32(expected_source)));
    ASSERT(strequal2(normalized->text + segment->normalized_start,
                     normalized_len,
                     normalized->text + segment->target_start,
                     target_len));

    return;
}



enum CtcTextReferenceCase {
    CTC_TEXT_REFERENCE_CASE_OTHER,
    CTC_TEXT_REFERENCE_CASE_PLAIN_ENGLISH,
    CTC_TEXT_REFERENCE_CASE_APOSTROPHES,
    CTC_TEXT_REFERENCE_CASE_PUNCTUATION_DIGITS,
    CTC_TEXT_REFERENCE_CASE_ACCENTS,
    CTC_TEXT_REFERENCE_CASE_BRACKETS_BLANK_LINES,
    CTC_TEXT_REFERENCE_CASE_PORTUGUESE_SOLTASBRUXA,
    CTC_TEXT_REFERENCE_CASE_GERMAN_ICH_WILL,
};

typedef struct CtcTextReferenceFixtureTotals {
    bool saw_format;
    bool saw_plain_english;
    bool saw_apostrophes;
    bool saw_punctuation_digits;
    bool saw_accents;
    bool saw_brackets_blank_lines;
    bool saw_portuguese_soltasbruxa;
    bool saw_german_ich_will;

    int32 fixture_count;
} CtcTextReferenceFixtureTotals;

typedef struct CtcTextReferenceFixtureCurrent {
    bool in_fixture;
    bool saw_language;
    bool saw_split_size;
    bool saw_effective_split_size;
    bool saw_romanize;
    bool saw_input;

    enum CtcTextReferenceCase case_id;

    int32 text_split_count;
    int32 normalized_count;
    int32 tokens_count;
    int32 edges_tokens_count;
    int32 edges_text_count;
    int32 segment_tokens_count;
    int32 segment_text_count;
} CtcTextReferenceFixtureCurrent;

static bool
ctc_text_reference_field_equal(
    char *field,
    int32 field_len,
    char *expected
) {
    return strequal2(field, field_len, expected, strlen32(expected));
}

static int32
ctc_text_reference_line_tab(
    char *line,
    int32 line_len
) {
    for (int32 i = 0; i < line_len; i += 1) {
        if (line[i] == '\t') {
            return i;
        }
    }

    return -1;
}

static void
ctc_text_reference_totals_mark_case(
    CtcTextReferenceFixtureTotals *totals,
    CtcTextReferenceFixtureCurrent *current,
    char *value,
    int32 value_len
) {
    if (ctc_text_reference_field_equal(value, value_len, "plain_english")) {
        current->case_id = CTC_TEXT_REFERENCE_CASE_PLAIN_ENGLISH;
        totals->saw_plain_english = true;
    } else if (ctc_text_reference_field_equal(value,
                                              value_len,
                                              "apostrophes")) {
        current->case_id = CTC_TEXT_REFERENCE_CASE_APOSTROPHES;
        totals->saw_apostrophes = true;
    } else if (ctc_text_reference_field_equal(value,
                                              value_len,
                                              "punctuation_digits")) {
        current->case_id = CTC_TEXT_REFERENCE_CASE_PUNCTUATION_DIGITS;
        totals->saw_punctuation_digits = true;
    } else if (ctc_text_reference_field_equal(value, value_len, "accents")) {
        current->case_id = CTC_TEXT_REFERENCE_CASE_ACCENTS;
        totals->saw_accents = true;
    } else if (ctc_text_reference_field_equal(value,
                                              value_len,
                                              "brackets_blank_lines")) {
        current->case_id = CTC_TEXT_REFERENCE_CASE_BRACKETS_BLANK_LINES;
        totals->saw_brackets_blank_lines = true;
    } else if (ctc_text_reference_field_equal(value,
                                              value_len,
                                              "portuguese_soltasbruxa")) {
        current->case_id = CTC_TEXT_REFERENCE_CASE_PORTUGUESE_SOLTASBRUXA;
        totals->saw_portuguese_soltasbruxa = true;
    } else if (ctc_text_reference_field_equal(value,
                                              value_len,
                                              "german_ich_will")) {
        current->case_id = CTC_TEXT_REFERENCE_CASE_GERMAN_ICH_WILL;
        totals->saw_german_ich_will = true;
    } else {
        current->case_id = CTC_TEXT_REFERENCE_CASE_OTHER;
    }

    return;
}

static void
ctc_text_reference_validate_current(
    CtcTextReferenceFixtureCurrent *current
) {
    ASSERT(current->in_fixture);
    ASSERT(current->saw_language);
    ASSERT(current->saw_split_size);
    ASSERT(current->saw_effective_split_size);
    ASSERT(current->saw_romanize);
    ASSERT(current->saw_input);
    ASSERT(current->text_split_count > 0);
    ASSERT(current->normalized_count == current->text_split_count);
    ASSERT(current->tokens_count == current->normalized_count);
    ASSERT(current->edges_tokens_count == current->tokens_count + 2);
    ASSERT(current->edges_text_count == current->text_split_count + 2);
    ASSERT(current->segment_tokens_count == current->tokens_count*2);
    ASSERT(current->segment_text_count == current->text_split_count*2);

    return;
}

static void
ctc_text_reference_parse_field(
    CtcTextReferenceFixtureTotals *totals,
    CtcTextReferenceFixtureCurrent *current,
    char *field,
    int32 field_len,
    char *value,
    int32 value_len
) {
    if (ctc_text_reference_field_equal(field, field_len, "format")) {
        ASSERT(!current->in_fixture);
        ASSERT(ctc_text_reference_field_equal(value, value_len, "1"));
        totals->saw_format = true;
    } else if (ctc_text_reference_field_equal(field, field_len, "fixture")) {
        ASSERT(!current->in_fixture);
        memset64(current, 0, SIZEOF(*current));
        current->in_fixture = true;
        totals->fixture_count += 1;
        ctc_text_reference_totals_mark_case(totals,
                                            current,
                                            value,
                                            value_len);
    } else if (ctc_text_reference_field_equal(field, field_len, "language")) {
        ASSERT(current->in_fixture);
        ASSERT(value_len == 3);
        current->saw_language = true;
    } else if (ctc_text_reference_field_equal(field, field_len, "split_size")) {
        ASSERT(current->in_fixture);
        ASSERT(ctc_text_reference_field_equal(value, value_len, "word"));
        current->saw_split_size = true;
    } else if (ctc_text_reference_field_equal(field,
                                              field_len,
                                              "effective_split_size")) {
        ASSERT(current->in_fixture);
        ASSERT(ctc_text_reference_field_equal(value, value_len, "word"));
        current->saw_effective_split_size = true;
    } else if (ctc_text_reference_field_equal(field, field_len, "romanize")) {
        ASSERT(current->in_fixture);
        ASSERT(ctc_text_reference_field_equal(value, value_len, "false"));
        current->saw_romanize = true;
    } else if (ctc_text_reference_field_equal(field, field_len, "input")) {
        ASSERT(current->in_fixture);
        ASSERT(value_len > 0);
        current->saw_input = true;
    } else if (ctc_text_reference_field_equal(field, field_len, "text_split")) {
        ASSERT(current->in_fixture);
        current->text_split_count += 1;
    } else if (ctc_text_reference_field_equal(field, field_len, "normalized")) {
        ASSERT(current->in_fixture);
        if (current->case_id == CTC_TEXT_REFERENCE_CASE_PLAIN_ENGLISH) {
            if (current->normalized_count == 0) {
                ASSERT(ctc_text_reference_field_equal(value,
                                                      value_len,
                                                      "68656c6c6f"));
            } else if (current->normalized_count == 1) {
                ASSERT(ctc_text_reference_field_equal(value,
                                                      value_len,
                                                      "776f726c64"));
            }
        }
        current->normalized_count += 1;
    } else if (ctc_text_reference_field_equal(field, field_len, "tokens")) {
        ASSERT(current->in_fixture);
        if (current->case_id == CTC_TEXT_REFERENCE_CASE_PLAIN_ENGLISH) {
            if (current->tokens_count == 0) {
                ASSERT(ctc_text_reference_field_equal(value,
                                                      value_len,
                                                      "682065206c206c206f"));
            } else if (current->tokens_count == 1) {
                ASSERT(ctc_text_reference_field_equal(value,
                                                      value_len,
                                                      "77206f2072206c2064"));
            }
        }
        current->tokens_count += 1;
    } else if (ctc_text_reference_field_equal(field,
                                              field_len,
                                              "edges_tokens")) {
        ASSERT(current->in_fixture);
        current->edges_tokens_count += 1;
    } else if (ctc_text_reference_field_equal(field, field_len, "edges_text")) {
        ASSERT(current->in_fixture);
        current->edges_text_count += 1;
    } else if (ctc_text_reference_field_equal(field,
                                              field_len,
                                              "segment_tokens")) {
        ASSERT(current->in_fixture);
        current->segment_tokens_count += 1;
    } else if (ctc_text_reference_field_equal(field,
                                              field_len,
                                              "segment_text")) {
        ASSERT(current->in_fixture);
        current->segment_text_count += 1;
    } else {
        ASSERT(false);
    }

    return;
}

static int32
ctc_text_test_reference_fixtures_load(void) {
    CtcTextReferenceFixtureTotals totals = {0};
    CtcTextReferenceFixtureCurrent current = {0};
    char *text;
    int32 text_len;
    int32 line_start;

    text = read_entire_file("testdata/ctc_text_reference_fixtures.txt",
                            &text_len);

    line_start = 0;
    for (int32 i = 0; i <= text_len; i += 1) {
        if ((i == text_len) || (text[i] == '\n')) {
            char *line;
            int32 line_len;
            int32 tab;

            line = text + line_start;
            line_len = i - line_start;
            if ((line_len > 0) && (line[line_len - 1] == '\r')) {
                line_len -= 1;
            }
            line_start = i + 1;

            if (line_len <= 0) {
                continue;
            }
            if (line[0] == '#') {
                continue;
            }
            if (ctc_text_reference_field_equal(line, line_len, "end")) {
                ctc_text_reference_validate_current(&current);
                memset64(&current, 0, SIZEOF(current));
                continue;
            }

            tab = ctc_text_reference_line_tab(line, line_len);
            ASSERT(tab > 0);
            ctc_text_reference_parse_field(&totals,
                                           &current,
                                           line,
                                           tab,
                                           line + tab + 1,
                                           line_len - tab - 1);
        }
    }

    ASSERT(!current.in_fixture);
    ASSERT(totals.saw_format);
    ASSERT(totals.fixture_count == 7);
    ASSERT(totals.saw_plain_english);
    ASSERT(totals.saw_apostrophes);
    ASSERT(totals.saw_punctuation_digits);
    ASSERT(totals.saw_accents);
    ASSERT(totals.saw_brackets_blank_lines);
    ASSERT(totals.saw_portuguese_soltasbruxa);
    ASSERT(totals.saw_german_ich_will);

    free2(text, text_len + 1);

    return 0;
}


#define CTC_TEXT_REFERENCE_WORD_INPUT_MAX 1024
#define CTC_TEXT_REFERENCE_WORD_MAX 64
#define CTC_TEXT_REFERENCE_WORD_TEXT_MAX 128

typedef struct CtcTextReferenceWordFixture {
    char input[CTC_TEXT_REFERENCE_WORD_INPUT_MAX];
    char text_split[CTC_TEXT_REFERENCE_WORD_MAX]
                   [CTC_TEXT_REFERENCE_WORD_TEXT_MAX];

    int32 input_len;
    int32 text_split_lens[CTC_TEXT_REFERENCE_WORD_MAX];
    int32 text_split_count;
} CtcTextReferenceWordFixture;

static int32
ctc_text_hex_value(char c) {
    if ((c >= '0') && (c <= '9')) {
        return c - '0';
    }
    if ((c >= 'a') && (c <= 'f')) {
        return c - 'a' + 10;
    }
    if ((c >= 'A') && (c <= 'F')) {
        return c - 'A' + 10;
    }

    return -1;
}

static int32
ctc_text_decode_hex(
    char *buffer,
    int32 buffer_cap,
    char *hex,
    int32 hex_len
) {
    int32 len;

    if ((hex_len % 2) != 0) {
        return -1;
    }

    len = hex_len/2;
    if (len >= buffer_cap) {
        return -1;
    }

    for (int32 i = 0; i < len; i += 1) {
        int32 hi;
        int32 lo;

        hi = ctc_text_hex_value(hex[i*2]);
        lo = ctc_text_hex_value(hex[i*2 + 1]);
        if ((hi < 0) || (lo < 0)) {
            return -1;
        }
        buffer[i] = (char)((hi << 4) | lo);
    }
    buffer[len] = '\0';

    return len;
}

static bool
ctc_text_reference_load_word_fixture(
    char *fixture_name,
    CtcTextReferenceWordFixture *fixture
) {
    char *text;
    int32 text_len;
    int32 line_start;
    bool in_fixture;
    bool matched_fixture;
    bool found_fixture;

    memset64(fixture, 0, SIZEOF(*fixture));

    text = read_entire_file("testdata/ctc_text_reference_fixtures.txt",
                            &text_len);
    ASSERT(text);

    line_start = 0;
    in_fixture = false;
    matched_fixture = false;
    found_fixture = false;
    for (int32 i = 0; i <= text_len; i += 1) {
        if ((i == text_len) || (text[i] == '\n')) {
            char *line;
            int32 line_len;
            int32 tab;

            line = text + line_start;
            line_len = i - line_start;
            if ((line_len > 0) && (line[line_len - 1] == '\r')) {
                line_len -= 1;
            }
            line_start = i + 1;

            if ((line_len <= 0) || (line[0] == '#')) {
                continue;
            }
            if (ctc_text_reference_field_equal(line, line_len, "end")) {
                if (matched_fixture) {
                    found_fixture = true;
                    break;
                }
                in_fixture = false;
                matched_fixture = false;
                continue;
            }

            tab = ctc_text_reference_line_tab(line, line_len);
            ASSERT(tab > 0);
            if (ctc_text_reference_field_equal(line, tab, "fixture")) {
                char *value;
                int32 value_len;

                value = line + tab + 1;
                value_len = line_len - tab - 1;
                in_fixture = true;
                matched_fixture = ctc_text_reference_field_equal(
                    value,
                    value_len,
                    fixture_name
                );
                continue;
            }
            if (!in_fixture || !matched_fixture) {
                continue;
            }

            if (ctc_text_reference_field_equal(line, tab, "input")) {
                fixture->input_len = ctc_text_decode_hex(
                    fixture->input,
                    CTC_TEXT_REFERENCE_WORD_INPUT_MAX,
                    line + tab + 1,
                    line_len - tab - 1
                );
                ASSERT(fixture->input_len >= 0);
            } else if (ctc_text_reference_field_equal(line,
                                                      tab,
                                                      "text_split")) {
                int32 index;

                index = fixture->text_split_count;
                ASSERT(index < CTC_TEXT_REFERENCE_WORD_MAX);
                fixture->text_split_lens[index] = ctc_text_decode_hex(
                    fixture->text_split[index],
                    CTC_TEXT_REFERENCE_WORD_TEXT_MAX,
                    line + tab + 1,
                    line_len - tab - 1
                );
                ASSERT(fixture->text_split_lens[index] >= 0);
                fixture->text_split_count += 1;
            }
        }
    }

    free2(text, text_len + 1);

    if (!found_fixture) {
        return false;
    }
    ASSERT(fixture->input_len > 0);
    ASSERT(fixture->text_split_count > 0);

    return true;
}

static int32
ctc_text_test_word_split_fixture_case(char *fixture_name) {
    CtcTextReferenceWordFixture fixture;
    LrcLyricsPreprocessOptions options;
    LrcLyricsNormalized normalized;
    LrcLyrics lyrics;

    if (!ctc_text_reference_load_word_fixture(fixture_name, &fixture)) {
        return ctc_text_test_fail("load word split fixture");
    }
    if (!ctc_text_load_lyrics(&lyrics, fixture.input, fixture_name)) {
        return ctc_text_test_fail("load word split fixture lyrics");
    }

    lrc_lyrics_preprocess_options_init(&options);
    options.split_size = LRC_LYRICS_PREPROCESS_SPLIT_SIZE_WORD;

    lrc_lyrics_normalized_init(&normalized);
    if (!lrc_lyrics_normalize_with_options(&lyrics, &normalized, &options)) {
        lrc_lyrics_destroy(&lyrics);
        return ctc_text_test_fail("normalize word split fixture lyrics");
    }

    ASSERT(normalized.segment_count == fixture.text_split_count);
    for (int32 i = 0; i < fixture.text_split_count; i += 1) {
        CtcTextSegment *segment;
        int32 source_len;

        segment = lrc_lyrics_normalized_segment(&normalized, i);
        ASSERT(segment);
        source_len = segment->source_end - segment->source_start;
        ASSERT(strequal2(lyrics.text + segment->source_start,
                         source_len,
                         fixture.text_split[i],
                         fixture.text_split_lens[i]));
    }

    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);

    return 0;
}

static int32
ctc_text_test_word_split_matches_reference_fixtures(void) {
    int32 status;

    status = 0;

    status += ctc_text_test_word_split_fixture_case("plain_english");
    status += ctc_text_test_word_split_fixture_case("apostrophes");
    status += ctc_text_test_word_split_fixture_case("punctuation_digits");
    status += ctc_text_test_word_split_fixture_case("accents");
    status += ctc_text_test_word_split_fixture_case("portuguese_soltasbruxa");
    status += ctc_text_test_word_split_fixture_case("german_ich_will");

    return status;
}

static int32
ctc_text_test_word_split_option_preserves_current_text(void) {
    LrcLyricsPreprocessOptions options;
    LrcLyricsNormalized current;
    LrcLyricsNormalized word;
    LrcLyrics lyrics;

    if (!ctc_text_load_lyrics(&lyrics,
                              "Hello, WORLD!\n[Verse]\nagain?! voce\n",
                              "ctc_text_word_options")) {
        return ctc_text_test_fail("load word option lyrics");
    }

    lrc_lyrics_preprocess_options_init(&options);
    options.split_size = LRC_LYRICS_PREPROCESS_SPLIT_SIZE_WORD;

    lrc_lyrics_normalized_init(&current);
    lrc_lyrics_normalized_init(&word);
    if (!lrc_lyrics_normalize(&lyrics, &current)) {
        lrc_lyrics_destroy(&lyrics);
        return ctc_text_test_fail("normalize current option text");
    }
    if (!lrc_lyrics_normalize_with_options(&lyrics, &word, &options)) {
        lrc_lyrics_normalized_destroy(&current);
        lrc_lyrics_destroy(&lyrics);
        return ctc_text_test_fail("normalize word option text");
    }

    ASSERT(strequal2(current.text,
                     current.text_len,
                     word.text,
                     word.text_len));
    ASSERT(current.segment_count == word.segment_count);
    ASSERT(word.alignable_line_count == 2);

    lrc_lyrics_normalized_destroy(&word);
    lrc_lyrics_normalized_destroy(&current);
    lrc_lyrics_destroy(&lyrics);

    return 0;
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
ctc_text_test_word_segments_preserve_line_mapping(void) {
    LrcLyrics lyrics;
    LrcLyricsNormalized normalized;

    if (!ctc_text_load_lyrics(&lyrics,
                              "Hello, WORLD!\n[Verse]\nagain?! voce\n",
                              "ctc_text_segments")) {
        return ctc_text_test_fail("load segment lyrics");
    }

    lrc_lyrics_normalized_init(&normalized);
    if (!lrc_lyrics_normalize(&lyrics, &normalized)) {
        lrc_lyrics_destroy(&lyrics);
        return ctc_text_test_fail("normalize segment lyrics");
    }

    ASSERT(strequal2(normalized.text,
                     normalized.text_len,
                     STRLIT("hello world again voce")));
    ASSERT(lrc_lyrics_normalized_segment_count(&normalized) == 4);
    ASSERT(lrc_lyrics_normalized_segment(&normalized, -1) == NULL);
    ASSERT(lrc_lyrics_normalized_segment(&normalized, 4) == NULL);

    ctc_text_test_assert_segment(&lyrics, &normalized, 0, 0, 0, 5, "Hello,");
    ctc_text_test_assert_segment(&lyrics, &normalized, 1, 0, 6, 11, "WORLD!");
    ctc_text_test_assert_segment(&lyrics, &normalized, 2, 2, 12, 17, "again?!");
    ctc_text_test_assert_segment(&lyrics, &normalized, 3, 2, 18, 22, "voce");

    lrc_lyrics_normalized_destroy(&normalized);
    lrc_lyrics_destroy(&lyrics);

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
    status += ctc_text_test_word_segments_preserve_line_mapping();
    status += ctc_text_test_current_normalization_mapping();
    status += ctc_text_test_reference_fixtures_load();
    status += ctc_text_test_word_split_option_preserves_current_text();
    status += ctc_text_test_word_split_matches_reference_fixtures();

    return status;
}
#endif /* TESTING_ctc_text */
