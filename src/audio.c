#include "audio.h"

void
audio_buffer_init(AudioBuffer *audio) {
    audio->left = 0;
    audio->right = 0;

    audio->frame_count = 0;
    audio->sample_rate = 0;
    audio->channel_count = 0;

    return;
}

void
audio_buffer_destroy(AudioBuffer *audio) {
    audio_buffer_init(audio);

    return;
}

bool
audio_read_file(AudioBuffer *audio, char *path, char *ffmpeg_path) {
    (void)audio;
    (void)path;
    (void)ffmpeg_path;

    return false;
}

bool
audio_write_file(
    AudioBuffer *audio,
    char *path,
    char *format,
    char *ffmpeg_path
) {
    (void)audio;
    (void)path;
    (void)format;
    (void)ffmpeg_path;

    return false;
}

#if TESTING_audio

int
main(void) {
    AudioBuffer audio;

    audio_buffer_init(&audio);
    audio_buffer_destroy(&audio);

    return 0;
}

#endif /* TESTING_audio */
