#if !defined(AUDIO_H)
#define AUDIO_H

#include "cbase.h"

#include <stdbool.h>

typedef struct AudioBuffer {
    float *left;
    float *right;

    int64 frame_count;
    int32 sample_rate;
    int32 channel_count;
} AudioBuffer;

void audio_buffer_init(AudioBuffer *audio);
void audio_buffer_destroy(AudioBuffer *audio);
bool audio_check_ffmpeg(char *ffmpeg_path);
bool audio_can_decode_file(char *path, char *ffmpeg_path);
bool audio_read_file(AudioBuffer *audio, char *path, char *ffmpeg_path);
bool audio_write_file(
    AudioBuffer *audio,
    char *path,
    char *format,
    char *ffmpeg_path
);

#endif /* AUDIO_H */
