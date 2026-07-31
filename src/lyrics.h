#if !defined(LYRICS_H)
#define LYRICS_H

#include "cbase.h"



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

enum LrcLyricsLoadError {
    LRC_LYRICS_LOAD_ERROR_NONE,
    LRC_LYRICS_LOAD_ERROR_INVALID_ARGUMENT,
    LRC_LYRICS_LOAD_ERROR_MISSING_PATH,
    LRC_LYRICS_LOAD_ERROR_OPEN_FAILED,
    LRC_LYRICS_LOAD_ERROR_READ_FAILED,
    LRC_LYRICS_LOAD_ERROR_FILE_TOO_LARGE,
    LRC_LYRICS_LOAD_ERROR_INVALID_UTF8,
    LRC_LYRICS_LOAD_ERROR_EMPTY,
};

typedef struct LrcLyricsLine {
    char *text;

    int32 text_len;
    int32 text_start;
    int32 text_end;
} LrcLyricsLine;

typedef struct LrcLyrics {
    char *text;
    LrcLyricsLine *lines;

    int32 text_len;
    int32 text_cap;
    int32 line_count;
    int32 line_cap;
    int32 nonempty_line_count;

    bool had_utf8_bom;
} LrcLyrics;

typedef struct LrcLyricsLoadResult {
    enum LrcLyricsLoadError error;
    char *message;
    char *path;

    int32 byte_offset;
} LrcLyricsLoadResult;

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

static void lrc_lyrics_init(LrcLyrics *lyrics);
static void lrc_lyrics_destroy(LrcLyrics *lyrics);
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
static void lrc_lyrics_load_result_init(LrcLyricsLoadResult *result);
static bool lrc_lyrics_load_file(
    LrcLyrics *lyrics,
    char *path,
    LrcLyricsLoadResult *result
);
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

#endif /* LYRICS_H */
