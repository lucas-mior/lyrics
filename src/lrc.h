#if !defined(LRC_H)
#define LRC_H

#include "cbase.h"

enum LrcParseError {
    LRC_PARSE_ERROR_NONE,
    LRC_PARSE_ERROR_INVALID_ARGUMENT,
    LRC_PARSE_ERROR_TOO_LARGE,
    LRC_PARSE_ERROR_MALFORMED_TIMESTAMP,
    LRC_PARSE_ERROR_UNTIMED_TEXT,
};

enum LrcParsedLineKind {
    LRC_PARSED_LINE_KIND_TIMESTAMPED,
    LRC_PARSED_LINE_KIND_BLANK,
};

typedef struct LrcParsedLine {
    char *text;

    int32 text_len;
    int32 source_line_index;
    int32 timestamp_hundredths;

    float timestamp_seconds;
    enum LrcParsedLineKind kind;
} LrcParsedLine;

typedef struct LrcParseResult {
    enum LrcParseError error;
    char *message;

    int32 line_index;
    int32 byte_offset;
} LrcParseResult;

typedef struct LrcParsedFile {
    char *text;
    LrcParsedLine *lines;

    int32 text_len;
    int32 text_cap;
    int32 line_count;
    int32 line_cap;
    int32 timestamped_line_count;
    int32 blank_line_count;
} LrcParsedFile;

static void lrc_parse_result_init(LrcParseResult *result);
static void lrc_parsed_file_init(LrcParsedFile *parsed);
static void lrc_parsed_file_destroy(LrcParsedFile *parsed);
static bool lrc_parse_text(
    LrcParsedFile *parsed,
    char *text,
    int32 text_len,
    LrcParseResult *result
);

#endif /* LRC_H */
