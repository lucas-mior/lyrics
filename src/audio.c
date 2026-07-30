#include "audio.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
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
    free(audio->left);
    free(audio->right);
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
    char *argv[] = {
        ffmpeg_path,
        "-v",
        "error",
        "-i",
        path,
        "-ac",
        "2",
        "-ar",
        "44100",
        "-f",
        "f32le",
        "-",
        0,
    };
    char *raw;
    float *samples;
    int pipe_fds[2];
    int status;
    int64 frame_bytes;
    int64 raw_capacity;
    int64 raw_len;
    int64 sample_count;
    pid_t pid;
    pid_t waited;

    audio_buffer_destroy(audio);

    if (pipe(pipe_fds) != 0) {
        return false;
    }

    pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return false;
    }

    if (pid == 0) {
        int null_fd;

        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        if (pipe_fds[1] > STDERR_FILENO) {
            close(pipe_fds[1]);
        }

        null_fd = open("/dev/null", O_RDONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDIN_FILENO);
            if (null_fd > STDERR_FILENO) {
                close(null_fd);
            }
        }

        execvp(argv[0], argv);
        _exit(127);
    }

    close(pipe_fds[1]);
    raw = 0;
    raw_len = 0;
    raw_capacity = 64*1024;
    raw = malloc((size_t)raw_capacity);
    if (raw == 0) {
        close(pipe_fds[0]);
        waitpid(pid, &status, 0);
        return false;
    }

    for (;;) {
        ssize_t bytes_read;

        if (raw_len == raw_capacity) {
            char *new_raw;
            int64 new_capacity;

            if (raw_capacity > INT64_MAX/2) {
                free(raw);
                close(pipe_fds[0]);
                waitpid(pid, &status, 0);
                return false;
            }

            new_capacity = 2*raw_capacity;
            new_raw = realloc(raw, (size_t)new_capacity);
            if (new_raw == 0) {
                free(raw);
                close(pipe_fds[0]);
                waitpid(pid, &status, 0);
                return false;
            }
            raw = new_raw;
            raw_capacity = new_capacity;
        }

        bytes_read = read(pipe_fds[0],
                          raw + raw_len,
                          (size_t)(raw_capacity - raw_len));
        if (bytes_read > 0) {
            raw_len += (int64)bytes_read;
            continue;
        }
        if (bytes_read == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }

        free(raw);
        close(pipe_fds[0]);
        waitpid(pid, &status, 0);
        return false;
    }

    close(pipe_fds[0]);
    do {
        waited = waitpid(pid, &status, 0);
    } while ((waited < 0) && (errno == EINTR));

    if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        free(raw);
        return false;
    }

    frame_bytes = 2*(int64)sizeof(float);
    if ((raw_len % frame_bytes) != 0) {
        free(raw);
        return false;
    }

    audio->frame_count = raw_len/frame_bytes;
    audio->sample_rate = 44100;
    audio->channel_count = 2;
    if (audio->frame_count == 0) {
        free(raw);
        return true;
    }

    sample_count = audio->frame_count*(int64)sizeof(*audio->left);
    audio->left = malloc((size_t)sample_count);
    audio->right = malloc((size_t)sample_count);
    if (audio->left == 0 || audio->right == 0) {
        free(raw);
        audio_buffer_destroy(audio);
        return false;
    }

    samples = (float *)raw;
    for (int64 i = 0; i < audio->frame_count; i += 1) {
        audio->left[i] = samples[2*i];
        audio->right[i] = samples[2*i + 1];
    }

    free(raw);

    return true;
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
    char script[128];
    char script_text[] =
        "#!/bin/sh\n"
        "if [ \"$1\" = \"-hide_banner\" ]; then\n"
        "    exit 0\n"
        "fi\n"
        "printf '\\000\\000\\200\\077\\000\\000\\000\\300'\n"
        "printf '\\000\\000\\140\\100\\000\\000\\210\\300'\n"
        "exit 0\n";
    int fd;

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

    if (snprintf(script, sizeof(script),
                 "/tmp/uvr_fake_ffmpeg_%lld",
                 (int64)getpid()) < 0) {
        return audio_test_fail("fake ffmpeg path");
    }
    fd = open(script, O_WRONLY|O_CREAT|O_EXCL, 0700);
    if (fd < 0) {
        return audio_test_fail("fake ffmpeg file");
    }
    if (write(fd, script_text, sizeof(script_text) - 1)
        != (ssize_t)(sizeof(script_text) - 1)) {
        close(fd);
        unlink(script);
        return audio_test_fail("fake ffmpeg write");
    }
    close(fd);
    if (chmod(script, 0700) != 0) {
        unlink(script);
        return audio_test_fail("fake ffmpeg chmod");
    }

    if (!audio_check_ffmpeg(script)) {
        unlink(script);
        return audio_test_fail("fake ffmpeg version");
    }
    if (!audio_can_decode_file("input.mp3", script)) {
        unlink(script);
        return audio_test_fail("fake ffmpeg decode check");
    }
    if (!audio_read_file(&audio, "input.mp3", script)) {
        unlink(script);
        return audio_test_fail("read fake audio");
    }
    unlink(script);

    if (audio.sample_rate != 44100) {
        audio_buffer_destroy(&audio);
        return audio_test_fail("sample rate");
    }
    if (audio.channel_count != 2) {
        audio_buffer_destroy(&audio);
        return audio_test_fail("channels");
    }
    if (audio.frame_count != 2) {
        audio_buffer_destroy(&audio);
        return audio_test_fail("decoded frame count");
    }
    if ((audio.left[0] != 1.0f) || (audio.right[0] != -2.0f)) {
        audio_buffer_destroy(&audio);
        return audio_test_fail("decoded first frame");
    }
    if ((audio.left[1] != 3.5f) || (audio.right[1] != -4.25f)) {
        audio_buffer_destroy(&audio);
        return audio_test_fail("decoded second frame");
    }
    audio_buffer_destroy(&audio);

    return 0;
}

#endif /* TESTING_audio */
