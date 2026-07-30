#include "audio.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

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

static bool
audio_run_process(char **argv) {
    int status;
    pid_t pid;
    pid_t waited;

    pid = fork();
    if (pid < 0) {
        return false;
    }

    if (pid == 0) {
        int null_fd;

        null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDOUT_FILENO);
            dup2(null_fd, STDERR_FILENO);
            if (null_fd > STDERR_FILENO) {
                close(null_fd);
            }
        }

        execvp(argv[0], argv);
        _exit(127);
    }

    do {
        waited = waitpid(pid, &status, 0);
    } while ((waited < 0) && (errno == EINTR));

    if (waited < 0) {
        return false;
    }
    if (!WIFEXITED(status)) {
        return false;
    }

    return WEXITSTATUS(status) == 0;
}

bool
audio_check_ffmpeg(char *ffmpeg_path) {
    char *argv[] = {
        ffmpeg_path,
        "-hide_banner",
        "-version",
        0,
    };

    return audio_run_process(argv);
}

bool
audio_can_decode_file(char *path, char *ffmpeg_path) {
    char *argv[] = {
        ffmpeg_path,
        "-v",
        "error",
        "-i",
        path,
        "-t",
        "0.1",
        "-f",
        "null",
        "-",
        0,
    };

    return audio_run_process(argv);
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

static int32
audio_test_fail(char *name) {
    fprintf(stderr, "audio test failed: %s\n", name);

    return 1;
}

int
main(void) {
    AudioBuffer audio;

    audio_buffer_init(&audio);
    if ((audio.left != 0) || (audio.right != 0)) {
        return audio_test_fail("buffer pointers");
    }
    if (audio.frame_count != 0) {
        return audio_test_fail("frame count");
    }
    audio_buffer_destroy(&audio);

    if (audio_check_ffmpeg("/definitely/missing/ffmpeg") != false) {
        return audio_test_fail("missing ffmpeg accepted");
    }
    if (audio_can_decode_file("missing.wav",
                              "/definitely/missing/ffmpeg") != false) {
        return audio_test_fail("missing ffmpeg decode accepted");
    }

    return 0;
}

#endif /* TESTING_audio */
