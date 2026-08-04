#if !defined(AUDIO_FORMATS_H)
#define AUDIO_FORMATS_H

#include "cbase.h"

#define LRC_AUDIO_FORMAT_DEFAULT "wav"
#define LRC_AUDIO_FORMAT_NAMES "wav|flac|mp3|opus"

#define LRC_AUDIO_FORMAT_VALUES(X) \
    X(LRC_AUDIO_FORMAT_WAV, "wav")  \
    X(LRC_AUDIO_FORMAT_FLAC, "flac") \
    X(LRC_AUDIO_FORMAT_MP3, "mp3")   \
    X(LRC_AUDIO_FORMAT_OPUS, "opus")

#define ENUM_NAME LrcAudioFormat
#define ENUM_BITFLAGS 0
#define ENUM_PREFIX_ LRC_AUDIO_FORMAT_
#define LRC_AUDIO_FORMAT_ENUM_FIELD(e, name) X(e)
#define ENUM_FIELDS LRC_AUDIO_FORMAT_VALUES(LRC_AUDIO_FORMAT_ENUM_FIELD)
#define XENUMS_NO_TESTS 1
#include "cbase/xenums.c"
#undef XENUMS_NO_TESTS
#undef LRC_AUDIO_FORMAT_ENUM_FIELD

typedef struct LrcAudioFormatInfo {
    enum LrcAudioFormat format;
    char *name;
} LrcAudioFormatInfo;

static LrcAudioFormatInfo lrc_audio_format_infos[] = {
#define LRC_AUDIO_FORMAT_INFO_ENTRY(e, name) {e, name},
    LRC_AUDIO_FORMAT_VALUES(LRC_AUDIO_FORMAT_INFO_ENTRY)
#undef LRC_AUDIO_FORMAT_INFO_ENTRY
};

static bool
lrc_audio_format_parse(char *value, enum LrcAudioFormat *format) {
    if ((value == NULL) || (format == NULL)) {
        return false;
    }

    for (int32 i = 0; i < LENGTH(lrc_audio_format_infos); i += 1) {
        if (strequal(value, lrc_audio_format_infos[i].name)) {
            *format = lrc_audio_format_infos[i].format;
            return true;
        }
    }

    return false;
}

static bool
lrc_audio_format_valid(char *value) {
    enum LrcAudioFormat format;

    return lrc_audio_format_parse(value, &format);
}

static bool
lrc_audio_format_is(char *value, enum LrcAudioFormat format) {
    enum LrcAudioFormat parsed;

    if (!lrc_audio_format_parse(value, &parsed)) {
        return false;
    }

    return parsed == format;
}

#endif /* AUDIO_FORMATS_H */
