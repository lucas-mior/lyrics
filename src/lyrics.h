#if !defined(LYRICS_H)
#define LYRICS_H

#include "cbase.h"
#include "ctc_text.h"


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

struct LrcLyrics {
    char *text;
    LrcLyricsLine *lines;

    int32 text_len;
    int32 text_cap;
    int32 line_count;
    int32 line_cap;
    int32 nonempty_line_count;

    bool had_utf8_bom;
};

typedef struct LrcLyricsLoadResult {
    enum LrcLyricsLoadError error;
    char *message;
    char *path;

    int32 byte_offset;
} LrcLyricsLoadResult;

static void lrc_lyrics_init(LrcLyrics *lyrics);
static void lrc_lyrics_destroy(LrcLyrics *lyrics);
static void lrc_lyrics_load_result_init(LrcLyricsLoadResult *result);
static bool lrc_lyrics_load_file(
    LrcLyrics *lyrics,
    char *path,
    LrcLyricsLoadResult *result
);

#endif /* LYRICS_H */
