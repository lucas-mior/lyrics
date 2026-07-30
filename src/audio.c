#include "audio.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
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
    char *argv[] = {
        ffmpeg_path,
        "-v",
        "error",
        "-y",
        "-f",
        "f32le",
        "-ar",
        "44100",
        "-ac",
        "2",
        "-i",
        "-",
        "-vn",
        "-ac",
        "2",
        "-ar",
        "44100",
        "-f",
        format,
        path,
        0,
    };
    float *interleaved;
    int pipe_fds[2];
    int status;
    int64 frame_capacity;
    int64 frame_offset;
    pid_t pid;
    pid_t waited;
    void (*previous_sigpipe)(int);

    if (audio == 0 || path == 0 || format == 0 || ffmpeg_path == 0) {
        return false;
    }
    if (audio->frame_count < 0 || audio->sample_rate != 44100
        || audio->channel_count != 2) {
        return false;
    }
    if (audio->frame_count > 0
        && (audio->left == 0 || audio->right == 0)) {
        return false;
    }

    frame_capacity = 4096;
    interleaved = malloc((size_t)(2*frame_capacity*sizeof(*interleaved)));
    if (interleaved == 0) {
        return false;
    }

    if (pipe(pipe_fds) != 0) {
        free(interleaved);
        return false;
    }

    pid = fork();
    if (pid < 0) {
        free(interleaved);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return false;
    }

    if (pid == 0) {
        int null_fd;

        close(pipe_fds[1]);
        dup2(pipe_fds[0], STDIN_FILENO);
        if (pipe_fds[0] > STDERR_FILENO) {
            close(pipe_fds[0]);
        }

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

    close(pipe_fds[0]);
    previous_sigpipe = signal(SIGPIPE, SIG_IGN);
    for (frame_offset = 0; frame_offset < audio->frame_count;) {
        char *bytes;
        int64 byte_count;
        int64 byte_offset;
        int64 chunk_frames;

        chunk_frames = audio->frame_count - frame_offset;
        if (chunk_frames > frame_capacity) {
            chunk_frames = frame_capacity;
        }

        for (int64 i = 0; i < chunk_frames; i += 1) {
            interleaved[2*i] = audio->left[frame_offset + i];
            interleaved[2*i + 1] = audio->right[frame_offset + i];
        }

        bytes = (char *)interleaved;
        byte_offset = 0;
        byte_count = 2*chunk_frames*(int64)sizeof(*interleaved);
        while (byte_offset < byte_count) {
            ssize_t bytes_written;

            bytes_written = write(pipe_fds[1],
                                  bytes + byte_offset,
                                  (size_t)(byte_count - byte_offset));
            if (bytes_written > 0) {
                byte_offset += (int64)bytes_written;
                continue;
            }
            if (bytes_written < 0 && errno == EINTR) {
                continue;
            }

            close(pipe_fds[1]);
            signal(SIGPIPE, previous_sigpipe);
            free(interleaved);
            waitpid(pid, &status, 0);
            return false;
        }

        frame_offset += chunk_frames;
    }

    close(pipe_fds[1]);
    signal(SIGPIPE, previous_sigpipe);
    free(interleaved);

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

#if TESTING_audio

static int32
audio_test_fail(char *name) {
    fprintf(stderr, "audio test failed: %s\n", name);

    return 1;
}

int
main(void) {
    AudioBuffer audio;
    char output[128];
    char output_raw[16];
    char expected_raw[] = {
        0x00,
        0x00,
        (char)0x80,
        0x3f,
        0x00,
        0x00,
        0x00,
        (char)0xc0,
        0x00,
        0x00,
        0x60,
        0x40,
        0x00,
        0x00,
        (char)0x88,
        (char)0xc0,
    };
    char script[128];
    char script_text[] =
        "#!/bin/sh\n"
        "if [ \"$1\" = \"-hide_banner\" ]; then\n"
        "    exit 0\n"
        "fi\n"
        "if [ \"$3\" = \"-y\" ]; then\n"
        "    out=\n"
        "    for arg do out=$arg; done\n"
        "    cat > \"$out\"\n"
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
    if (snprintf(output, sizeof(output),
                 "/tmp/uvr_fake_audio_%lld",
                 (int64)getpid()) < 0) {
        return audio_test_fail("fake output path");
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

    unlink(output);
    if (!audio_write_file(&audio, output, "wav", script)) {
        audio_buffer_destroy(&audio);
        unlink(script);
        return audio_test_fail("write fake audio");
    }
    fd = open(output, O_RDONLY);
    if (fd < 0) {
        audio_buffer_destroy(&audio);
        unlink(script);
        return audio_test_fail("open fake output");
    }
    if (read(fd, output_raw, sizeof(output_raw))
        != (ssize_t)sizeof(output_raw)) {
        close(fd);
        audio_buffer_destroy(&audio);
        unlink(script);
        unlink(output);
        return audio_test_fail("read fake output");
    }
    close(fd);
    for (int32 i = 0; i < (int32)sizeof(output_raw); i += 1) {
        if (output_raw[i] != expected_raw[i]) {
            audio_buffer_destroy(&audio);
            unlink(script);
            unlink(output);
            return audio_test_fail("output interleaving");
        }
    }

    audio_buffer_destroy(&audio);
    unlink(script);
    unlink(output);

    return 0;
}

#endif /* TESTING_audio */
