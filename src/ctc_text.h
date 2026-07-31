#if !defined(CTC_TEXT_H)
#define CTC_TEXT_H

#include "cbase.h"


typedef struct LrcLyrics LrcLyrics;

enum LrcLyricsPreprocessSplitSize {
    LRC_LYRICS_PREPROCESS_SPLIT_SIZE_CURRENT,
    LRC_LYRICS_PREPROCESS_SPLIT_SIZE_WORD,
    LRC_LYRICS_PREPROCESS_SPLIT_SIZE_CHAR,
    LRC_LYRICS_PREPROCESS_SPLIT_SIZE_SENTENCE,
};

enum LrcLyricsPreprocessStarFrequency {
    LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_NONE,
    LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_EDGES,
    LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_SEGMENT,
};

enum LrcLyricsPreprocessRomanization {
    LRC_LYRICS_PREPROCESS_ROMANIZATION_OFF,
    LRC_LYRICS_PREPROCESS_ROMANIZATION_ICU,
};

typedef struct LrcLyricsPreprocessOptions {
    enum LrcLyricsPreprocessSplitSize split_size;
    enum LrcLyricsPreprocessStarFrequency star_frequency;
    enum LrcLyricsPreprocessRomanization romanization;
} LrcLyricsPreprocessOptions;

typedef struct LrcLyricsNormalizedByte {
    int32 line_index;
    int32 source_start;
    int32 source_end;
} LrcLyricsNormalizedByte;

enum LrcLyricsNormalizedLineKind {
    LRC_LYRICS_NORMALIZED_LINE_KIND_BLANK,
    LRC_LYRICS_NORMALIZED_LINE_KIND_SECTION_MARKER,
    LRC_LYRICS_NORMALIZED_LINE_KIND_PUNCTUATION_ONLY,
    LRC_LYRICS_NORMALIZED_LINE_KIND_ALIGNABLE,
};

typedef struct LrcLyricsNormalizedLine {
    enum LrcLyricsNormalizedLineKind kind;

    int32 normalized_start;
    int32 normalized_end;
} LrcLyricsNormalizedLine;

typedef struct LrcLyricsNormalized {
    char *text;
    LrcLyricsNormalizedByte *bytes;
    LrcLyricsNormalizedLine *lines;

    int32 text_len;
    int32 text_cap;
    int32 byte_count;
    int32 byte_cap;
    int32 line_count;
    int32 line_cap;
    int32 alignable_line_count;
} LrcLyricsNormalized;

static void
lrc_lyrics_preprocess_options_init(
    LrcLyricsPreprocessOptions *options
) {
    if (options == NULL) {
        return;
    }

    memset64(options, 0, SIZEOF(*options));

    options->split_size = LRC_LYRICS_PREPROCESS_SPLIT_SIZE_CURRENT;
    options->star_frequency = LRC_LYRICS_PREPROCESS_STAR_FREQUENCY_EDGES;
    options->romanization = LRC_LYRICS_PREPROCESS_ROMANIZATION_OFF;

    return;
}
static void lrc_lyrics_normalized_init(LrcLyricsNormalized *normalized);
static void lrc_lyrics_normalized_destroy(LrcLyricsNormalized *normalized);
static bool lrc_lyrics_normalize_with_options(
    LrcLyrics *lyrics,
    LrcLyricsNormalized *normalized,
    LrcLyricsPreprocessOptions *options
);
static bool lrc_lyrics_normalize(
    LrcLyrics *lyrics,
    LrcLyricsNormalized *normalized
);
static int32 lrc_lyrics_normalized_line_at(
    LrcLyricsNormalized *normalized,
    int32 byte_offset
);
static enum LrcLyricsNormalizedLineKind lrc_lyrics_normalized_line_kind(
    LrcLyricsNormalized *normalized,
    int32 line_index
);
static bool lrc_lyrics_normalized_line_range(
    LrcLyricsNormalized *normalized,
    int32 line_index,
    int32 *start,
    int32 *end
);

#endif /* CTC_TEXT_H */
