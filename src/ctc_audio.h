#if !defined(CTC_AUDIO_H)
#define CTC_AUDIO_H

#include "cbase.h"
#include "audio.h"

#define LRC_CTC_AUDIO_DEFAULT_SAMPLE_RATE 16000


enum LrcCtcAudioError {
    LRC_CTC_AUDIO_ERROR_NONE,
    LRC_CTC_AUDIO_ERROR_INVALID_ARGUMENT,
    LRC_CTC_AUDIO_ERROR_MISSING_PATH,
    LRC_CTC_AUDIO_ERROR_MISSING_FFMPEG,
    LRC_CTC_AUDIO_ERROR_INVALID_SAMPLE_RATE,
    LRC_CTC_AUDIO_ERROR_DECODE_FAILED,
    LRC_CTC_AUDIO_ERROR_EMPTY_AUDIO,
    LRC_CTC_AUDIO_ERROR_NON_FINITE_SAMPLE,
};

typedef struct LrcCtcAudioConfig {
    char *ffmpeg_path;

    int32 sample_rate;
} LrcCtcAudioConfig;

typedef struct LrcCtcAudioResult {
    enum LrcCtcAudioError error;
    char *message;
    char *path;

    int64 sample_index;
} LrcCtcAudioResult;

typedef struct LrcCtcAudio {
    float *samples;

    int64 sample_count;
    int32 sample_rate;
    int32 channel_count;
    double duration_seconds;
    float max_abs_sample;
} LrcCtcAudio;

static void lrc_ctc_audio_config_init(LrcCtcAudioConfig *config);
static void lrc_ctc_audio_result_init(LrcCtcAudioResult *result);
static void lrc_ctc_audio_destroy(LrcCtcAudio *audio);
static bool lrc_ctc_audio_decode_file(
    LrcCtcAudio *audio,
    char *path,
    LrcCtcAudioConfig *config,
    LrcCtcAudioResult *result
);

#endif /* CTC_AUDIO_H */
