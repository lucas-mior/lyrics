#if !defined(AUDIO_H)
#define AUDIO_H

#include "cbase.h"

typedef struct AudioIoFormat {
    int32 sample_rate;
    int32 channel_count;
} AudioIoFormat;

typedef struct AudioFileInfo {
    int32 sample_rate;
    int32 channel_count;
    int64 estimated_frame_count;

    double duration_seconds;
} AudioFileInfo;

typedef struct AudioBuffer {
    float *left;
    float *right;

    int64 frame_count;
    int32 sample_rate;
    int32 channel_count;
} AudioBuffer;

#define ENUM_NAME AudioCompareMode
#define ENUM_BITFLAGS 0
#define ENUM_PREFIX_ AUDIO_COMPARE_MODE_
#define ENUM_FIELDS \
    X(AUDIO_COMPARE_MODE_STRICT)          \
    X(AUDIO_COMPARE_MODE_TOLERANT)        \
    X(AUDIO_COMPARE_MODE_OFFSET_TOLERANT) \
    X(AUDIO_COMPARE_MODE_SNR)
#define XENUMS_NO_TESTS 1
#include "cbase/xenums.c"
#undef XENUMS_NO_TESTS

typedef struct AudioCompareOptions {
    enum AudioCompareMode mode;

    int64 max_offset_frames;
    int64 max_length_delta_frames;

    float max_abs_error;
    float max_rms_error;
    double min_snr_db;
} AudioCompareOptions;

typedef struct AudioCompareResult {
    enum AudioCompareMode mode;

    bool decoded;
    bool valid;
    bool finite;
    bool length_ok;
    bool passed;

    int64 expected_frames;
    int64 actual_frames;
    int64 compared_frames;
    int64 compared_samples;
    int64 length_delta_frames;
    int64 best_offset_frames;
    int64 nan_samples;
    int64 infinite_samples;

    float max_abs_error;
    float rms_error;
    float expected_peak;
    float actual_peak;
    double snr_db;
} AudioCompareResult;

#if TESTING
typedef struct AudioTestSineOptions {
    AudioIoFormat format;

    double duration_seconds;
    double frequency_hz;
    float amplitude;
} AudioTestSineOptions;
#endif

static void audio_io_format_init(AudioIoFormat *format);
static bool audio_io_format_valid(AudioIoFormat *format);
static void audio_file_info_init(AudioFileInfo *info);
static bool audio_file_info_read(
    AudioFileInfo *info,
    char *path,
    char *ffprobe_path
);
static void audio_buffer_init(AudioBuffer *audio);
static void audio_buffer_destroy(AudioBuffer *audio);
static bool audio_check_ffmpeg(char *ffmpeg_path);
static bool audio_can_decode_file(char *path, char *ffmpeg_path);
static bool audio_read_file(AudioBuffer *audio, char *path, char *ffmpeg_path);
static bool audio_read_file_format(
    AudioBuffer *audio,
    char *path,
    AudioIoFormat *format,
    char *ffmpeg_path
);
static bool audio_write_file(
    AudioBuffer *audio,
    char *path,
    char *format,
    char *ffmpeg_path
);
static bool audio_write_file_format(
    AudioBuffer *audio,
    char *path,
    char *container_format,
    AudioIoFormat *output_format,
    char *ffmpeg_path
);
static void audio_compare_options_init(AudioCompareOptions *options);
static void audio_compare_result_init(AudioCompareResult *result);
static bool audio_compare_buffers(
    AudioCompareResult *result,
    AudioBuffer *expected,
    AudioBuffer *actual,
    AudioCompareOptions *options
);
static bool audio_compare_files(
    AudioCompareResult *result,
    char *expected_path,
    char *actual_path,
    AudioCompareOptions *options,
    char *ffmpeg_path
);
static bool audio_compare_reconstruction_buffers(
    AudioCompareResult *result,
    AudioBuffer *mixture,
    AudioBuffer *first_stem,
    AudioBuffer *second_stem,
    AudioCompareOptions *options
);
static void audio_compare_result_print(
    AudioCompareResult *result,
    char *name
);
#if TESTING
static void audio_test_sine_options_init(AudioTestSineOptions *options);
static bool audio_test_generate_sine_wav(
    char *path,
    AudioTestSineOptions *options,
    char *ffmpeg_path
);
#endif

#endif /* AUDIO_H */
