#include "lrc.h"

#include "cbase.h"

#if !defined(TESTING_lrc)
#define TESTING_lrc 0
#endif

static void
lrc_parse_result_init(LrcParseResult *result) {
    if (result == NULL) {
        return;
    }

    result->error = LRC_PARSE_ERROR_NONE;
    result->message = "ok";

    result->line_index = -1;
    result->byte_offset = -1;

    return;
}

static void
lrc_parse_result_set(
    LrcParseResult *result,
    enum LrcParseError error,
    char *message,
    int32 line_index,
    int32 byte_offset
) {
    if (result == NULL) {
        return;
    }

    result->error = error;
    result->message = message;

    result->line_index = line_index;
    result->byte_offset = byte_offset;

    return;
}

static void
lrc_parsed_file_init(LrcParsedFile *parsed) {
    if (parsed == NULL) {
        return;
    }

    memset64(parsed, 0, SIZEOF(*parsed));

    return;
}

static void
lrc_parsed_file_destroy(LrcParsedFile *parsed) {
    if (parsed == NULL) {
        return;
    }

    if (parsed->text) {
        free2(parsed->text, parsed->text_cap*SIZEOF(*parsed->text));
    }
    if (parsed->lines) {
        free2(parsed->lines, parsed->line_cap*SIZEOF(*parsed->lines));
    }

    lrc_parsed_file_init(parsed);

    return;
}

static bool
lrc_is_digit(char c) {
    if ((c >= '0') && (c <= '9')) {
        return true;
    }

    return false;
}

static bool
lrc_is_blank_line(char *text, int32 text_len) {
    for (int32 i = 0; i < text_len; i += 1) {
        if ((text[i] != ' ') && (text[i] != '\t')) {
            return false;
        }
    }

    return true;
}

static bool
lrc_parse_two_digits(char *text, int32 index, int32 text_len, int32 *out) {
    if ((index + 1) >= text_len) {
        return false;
    }
    if (!lrc_is_digit(text[index]) || !lrc_is_digit(text[index + 1])) {
        return false;
    }

    *out = (text[index] - '0')*10 + (text[index + 1] - '0');

    return true;
}

static bool
lrc_parse_timestamp(
    char *text,
    int32 text_len,
    int32 *timestamp_hundredths,
    int32 *end_offset
) {
    int32 minutes;
    int32 seconds;
    int32 hundredths;
    int32 i;

    if ((text_len < 10) || (text[0] != '[')) {
        return false;
    }

    minutes = 0;
    i = 1;
    if ((i >= text_len) || !lrc_is_digit(text[i])) {
        return false;
    }
    while ((i < text_len) && lrc_is_digit(text[i])) {
        int32 digit;

        digit = text[i] - '0';
        if (minutes > (INT32_MAX - digit)/10) {
            return false;
        }
        minutes = minutes*10 + digit;
        i += 1;
    }
    if ((i >= text_len) || (text[i] != ':')) {
        return false;
    }
    i += 1;

    if (!lrc_parse_two_digits(text, i, text_len, &seconds)) {
        return false;
    }
    if (seconds >= 60) {
        return false;
    }
    i += 2;

    if ((i >= text_len) || (text[i] != '.')) {
        return false;
    }
    i += 1;

    if (!lrc_parse_two_digits(text, i, text_len, &hundredths)) {
        return false;
    }
    i += 2;

    if ((i >= text_len) || (text[i] != ']')) {
        return false;
    }
    i += 1;

    if (minutes > (INT32_MAX/100 - seconds)/60) {
        return false;
    }

    *timestamp_hundredths = (minutes*60 + seconds)*100 + hundredths;
    *end_offset = i;

    return true;
}

static bool
lrc_parsed_file_reserve_lines(LrcParsedFile *parsed, int32 extra) {
    int64 needed;
    int32 new_cap;

    if (extra <= 0) {
        return true;
    }

    needed = (int64)parsed->line_count + extra;
    if (needed <= parsed->line_cap) {
        return true;
    }
    if (needed >= MAXOF(parsed->line_cap)) {
        return false;
    }

    new_cap = parsed->line_cap;
    if (new_cap <= 0) {
        new_cap = 8;
    }
    while (new_cap < needed) {
        new_cap *= 2;
    }

    parsed->lines = realloc2(parsed->lines,
                             parsed->line_cap,
                             new_cap,
                             SIZEOF(*parsed->lines));
    parsed->line_cap = new_cap;

    return true;
}

static bool
lrc_parsed_file_append_line(
    LrcParsedFile *parsed,
    enum LrcParsedLineKind kind,
    int32 source_line_index,
    int32 timestamp_hundredths,
    int32 text_start,
    int32 text_len
) {
    LrcParsedLine *line;

    if (!lrc_parsed_file_reserve_lines(parsed, 1)) {
        return false;
    }

    line = &parsed->lines[parsed->line_count];
    parsed->line_count += 1;

    line->text = parsed->text + text_start;
    line->text_len = text_len;
    line->source_line_index = source_line_index;
    line->timestamp_hundredths = timestamp_hundredths;
    line->timestamp_seconds = (float)timestamp_hundredths/100.0f;
    line->kind = kind;

    if (kind == LRC_PARSED_LINE_KIND_TIMESTAMPED) {
        parsed->timestamped_line_count += 1;
    } else {
        parsed->blank_line_count += 1;
    }

    return true;
}

static bool
lrc_parse_line(
    LrcParsedFile *parsed,
    int32 source_line_index,
    int32 line_start,
    int32 line_end,
    LrcParseResult *result
) {
    char *line_text;
    int32 line_len;
    int32 timestamp_hundredths;
    int32 timestamp_end;

    line_text = parsed->text + line_start;
    line_len = line_end - line_start;
    if ((line_len > 0) && (line_text[line_len - 1] == '\r')) {
        line_len -= 1;
        parsed->text[line_start + line_len] = '\0';
    }

    if (line_len <= 0) {
        return lrc_parsed_file_append_line(parsed,
                                           LRC_PARSED_LINE_KIND_BLANK,
                                           source_line_index,
                                           -1,
                                           line_start,
                                           0);
    }
    if (lrc_is_blank_line(line_text, line_len)) {
        return lrc_parsed_file_append_line(parsed,
                                           LRC_PARSED_LINE_KIND_BLANK,
                                           source_line_index,
                                           -1,
                                           line_start,
                                           line_len);
    }
    if (line_text[0] != '[') {
        lrc_parse_result_set(result,
                             LRC_PARSE_ERROR_UNTIMED_TEXT,
                             "LRC line is missing timestamp",
                             source_line_index,
                             line_start);
        return false;
    }
    if (!lrc_parse_timestamp(line_text,
                             line_len,
                             &timestamp_hundredths,
                             &timestamp_end)) {
        lrc_parse_result_set(result,
                             LRC_PARSE_ERROR_MALFORMED_TIMESTAMP,
                             "LRC timestamp is malformed",
                             source_line_index,
                             line_start);
        return false;
    }

    return lrc_parsed_file_append_line(parsed,
                                       LRC_PARSED_LINE_KIND_TIMESTAMPED,
                                       source_line_index,
                                       timestamp_hundredths,
                                       line_start + timestamp_end,
                                       line_len - timestamp_end);
}

static bool
lrc_parsed_file_copy_text(
    LrcParsedFile *parsed,
    char *text,
    int32 text_len,
    LrcParseResult *result
) {
    if (text_len < 0) {
        lrc_parse_result_set(result,
                             LRC_PARSE_ERROR_INVALID_ARGUMENT,
                             "LRC text length is negative",
                             -1,
                             -1);
        return false;
    }
    if (text_len >= INT32_MAX) {
        lrc_parse_result_set(result,
                             LRC_PARSE_ERROR_TOO_LARGE,
                             "LRC text is too large",
                             -1,
                             -1);
        return false;
    }

    parsed->text = malloc2((int64)text_len + 1);
    if (text_len > 0) {
        memcpy64(parsed->text, text, text_len);
    }
    parsed->text[text_len] = '\0';
    parsed->text_len = text_len;
    parsed->text_cap = text_len + 1;

    return true;
}

static bool
lrc_parse_text(
    LrcParsedFile *parsed,
    char *text,
    int32 text_len,
    LrcParseResult *result
) {
    int32 line_start;
    int32 line_index;

    if (result) {
        lrc_parse_result_init(result);
    }
    if (parsed == NULL) {
        lrc_parse_result_set(result,
                             LRC_PARSE_ERROR_INVALID_ARGUMENT,
                             "LRC parsed-file destination is missing",
                             -1,
                             -1);
        return false;
    }
    if ((text == NULL) && (text_len > 0)) {
        lrc_parse_result_set(result,
                             LRC_PARSE_ERROR_INVALID_ARGUMENT,
                             "LRC text is missing",
                             -1,
                             -1);
        return false;
    }

    lrc_parsed_file_destroy(parsed);
    if (!lrc_parsed_file_copy_text(parsed, text, text_len, result)) {
        return false;
    }

    line_start = 0;
    line_index = 0;
    for (int32 i = 0; i < parsed->text_len; i += 1) {
        if (parsed->text[i] == '\n') {
            if (!lrc_parse_line(parsed,
                                line_index,
                                line_start,
                                i,
                                result)) {
                lrc_parsed_file_destroy(parsed);
                return false;
            }
            line_index += 1;
            line_start = i + 1;
        }
    }

    if ((line_start < parsed->text_len) || (parsed->text_len == 0)) {
        if (!lrc_parse_line(parsed,
                            line_index,
                            line_start,
                            parsed->text_len,
                            result)) {
            lrc_parsed_file_destroy(parsed);
            return false;
        }
    }

    return true;
}

#if TESTING_lrc

#define CBASE_IMPLEMENT
#include "cbase.h"

static int32
lrc_test_fail(char *name) {
    error2("lrc test failed: %s\n", name);

    return 1;
}

static void
lrc_test_assert_line(
    LrcParsedFile *parsed,
    int32 line_index,
    enum LrcParsedLineKind expected_kind,
    int32 expected_hundredths,
    char *expected_text,
    int32 expected_text_len
) {
    LrcParsedLine *line;

    ASSERT(line_index >= 0);
    ASSERT(line_index < parsed->line_count);

    line = &parsed->lines[line_index];
    ASSERT(line->kind == expected_kind);
    ASSERT(line->timestamp_hundredths == expected_hundredths);
    ASSERT(strequal2(line->text,
                     line->text_len,
                     expected_text,
                     expected_text_len));

    return;
}

static int32
lrc_test_parse_timestamped_and_blank_lines(void) {
    LrcParsedFile parsed;
    LrcParseResult result;
    char text[] = "[00:00.20]Hello\n\n[01:02.34]World\n";

    lrc_parsed_file_init(&parsed);
    if (!lrc_parse_text(&parsed, text, strlen32(text), &result)) {
        return lrc_test_fail("parse timestamped and blank lines");
    }

    ASSERT(parsed.line_count == 3);
    ASSERT(parsed.timestamped_line_count == 2);
    ASSERT(parsed.blank_line_count == 1);
    lrc_test_assert_line(&parsed,
                         0,
                         LRC_PARSED_LINE_KIND_TIMESTAMPED,
                         20,
                         STRLIT("Hello"));
    lrc_test_assert_line(&parsed,
                         1,
                         LRC_PARSED_LINE_KIND_BLANK,
                         -1,
                         STRLIT(""));
    lrc_test_assert_line(&parsed,
                         2,
                         LRC_PARSED_LINE_KIND_TIMESTAMPED,
                         6234,
                         STRLIT("World"));
    ASSERT(parsed.lines[2].timestamp_seconds == 62.34f);

    lrc_parsed_file_destroy(&parsed);

    return 0;
}

static int32
lrc_test_parse_crlf_and_space_blank_line(void) {
    LrcParsedFile parsed;
    LrcParseResult result;
    char text[] = "[00:01.00]One\r\n  \t\r\n[00:02.00]Two\r\n";

    lrc_parsed_file_init(&parsed);
    if (!lrc_parse_text(&parsed, text, strlen32(text), &result)) {
        return lrc_test_fail("parse crlf and blank line");
    }

    ASSERT(parsed.line_count == 3);
    ASSERT(parsed.timestamped_line_count == 2);
    ASSERT(parsed.blank_line_count == 1);
    lrc_test_assert_line(&parsed,
                         0,
                         LRC_PARSED_LINE_KIND_TIMESTAMPED,
                         100,
                         STRLIT("One"));
    lrc_test_assert_line(&parsed,
                         1,
                         LRC_PARSED_LINE_KIND_BLANK,
                         -1,
                         STRLIT("  \t"));
    lrc_test_assert_line(&parsed,
                         2,
                         LRC_PARSED_LINE_KIND_TIMESTAMPED,
                         200,
                         STRLIT("Two"));

    lrc_parsed_file_destroy(&parsed);

    return 0;
}

static int32
lrc_test_reject_malformed_timestamps(void) {
    LrcParsedFile parsed;
    LrcParseResult result;
    char bad_seconds[] = "[00:60.00]Bad\n";
    char bad_fraction[] = "[00:00.1]Bad\n";

    lrc_parsed_file_init(&parsed);
    if (lrc_parse_text(&parsed,
                       bad_seconds,
                       strlen32(bad_seconds),
                       &result)) {
        lrc_parsed_file_destroy(&parsed);
        return lrc_test_fail("accepted bad seconds");
    }
    ASSERT(result.error == LRC_PARSE_ERROR_MALFORMED_TIMESTAMP);
    ASSERT(result.line_index == 0);

    lrc_parsed_file_init(&parsed);
    if (lrc_parse_text(&parsed,
                       bad_fraction,
                       strlen32(bad_fraction),
                       &result)) {
        lrc_parsed_file_destroy(&parsed);
        return lrc_test_fail("accepted bad fraction");
    }
    ASSERT(result.error == LRC_PARSE_ERROR_MALFORMED_TIMESTAMP);
    ASSERT(result.line_index == 0);

    return 0;
}

static int32
lrc_test_reject_untimed_text(void) {
    LrcParsedFile parsed;
    LrcParseResult result;
    char text[] = "[00:01.00]Good\nUntimed lyric\n";

    lrc_parsed_file_init(&parsed);
    if (lrc_parse_text(&parsed, text, strlen32(text), &result)) {
        lrc_parsed_file_destroy(&parsed);
        return lrc_test_fail("accepted untimed text");
    }
    ASSERT(result.error == LRC_PARSE_ERROR_UNTIMED_TEXT);
    ASSERT(result.line_index == 1);

    return 0;
}

static int32
lrc_test_duplicate_timestamps_are_preserved(void) {
    LrcParsedFile parsed;
    LrcParseResult result;
    char text[] = "[00:01.00]A\n[00:01.00]B\n";

    lrc_parsed_file_init(&parsed);
    if (!lrc_parse_text(&parsed, text, strlen32(text), &result)) {
        return lrc_test_fail("parse duplicate timestamps");
    }

    ASSERT(parsed.line_count == 2);
    ASSERT(parsed.timestamped_line_count == 2);
    lrc_test_assert_line(&parsed,
                         0,
                         LRC_PARSED_LINE_KIND_TIMESTAMPED,
                         100,
                         STRLIT("A"));
    lrc_test_assert_line(&parsed,
                         1,
                         LRC_PARSED_LINE_KIND_TIMESTAMPED,
                         100,
                         STRLIT("B"));

    lrc_parsed_file_destroy(&parsed);

    return 0;
}

static int32
lrc_test_optional_maxwell_lrc(void) {
    LrcParsedFile parsed;
    LrcParseResult result;
    char *path;
    char *text;
    int32 text_len;

    path = getenv("LRC_TEST_MAXWELL_LRC");
    if (path == NULL) {
        path = "next-phase/maxwell.lrc";
    }
    if (!util_file_exists(path)) {
        return 0;
    }

    text = read_entire_file(path, &text_len);
    lrc_parsed_file_init(&parsed);
    if (!lrc_parse_text(&parsed, text, text_len, &result)) {
        free2(text, ((int64)text_len + 1)*SIZEOF(*text));
        return lrc_test_fail("parse maxwell lrc");
    }

    ASSERT(parsed.line_count == 6);
    ASSERT(parsed.timestamped_line_count == 5);
    ASSERT(parsed.blank_line_count == 1);
    lrc_test_assert_line(&parsed,
                         0,
                         LRC_PARSED_LINE_KIND_TIMESTAMPED,
                         20,
                         STRLIT("Can I take you out to the pictures, Joan?"));
    lrc_test_assert_line(&parsed,
                         1,
                         LRC_PARSED_LINE_KIND_TIMESTAMPED,
                         684,
                         STRLIT("But as she's getting ready to go"));
    lrc_test_assert_line(&parsed,
                         2,
                         LRC_PARSED_LINE_KIND_TIMESTAMPED,
                         1064,
                         STRLIT("A knock comes on the door"));
    lrc_test_assert_line(&parsed,
                         3,
                         LRC_PARSED_LINE_KIND_BLANK,
                         -1,
                         STRLIT(""));
    lrc_test_assert_line(&parsed,
                         4,
                         LRC_PARSED_LINE_KIND_TIMESTAMPED,
                         1414,
                         STRLIT("Bang, bang, Maxwell's silver hammer"));
    lrc_test_assert_line(&parsed,
                         5,
                         LRC_PARSED_LINE_KIND_TIMESTAMPED,
                         1800,
                         STRLIT("Came down upon her head"));

    lrc_parsed_file_destroy(&parsed);
    free2(text, ((int64)text_len + 1)*SIZEOF(*text));

    return 0;
}

int32
main(void) {
    if (lrc_test_parse_timestamped_and_blank_lines() != 0) {
        exit(1);
    }
    if (lrc_test_parse_crlf_and_space_blank_line() != 0) {
        exit(1);
    }
    if (lrc_test_reject_malformed_timestamps() != 0) {
        exit(1);
    }
    if (lrc_test_reject_untimed_text() != 0) {
        exit(1);
    }
    if (lrc_test_duplicate_timestamps_are_preserved() != 0) {
        exit(1);
    }
    if (lrc_test_optional_maxwell_lrc() != 0) {
        exit(1);
    }

    return 0;
}

#endif /* TESTING_lrc */
