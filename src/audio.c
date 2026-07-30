#include "audio.h"

#include "cbase.h"

#if !defined(TESTING_audio)
#define TESTING_audio 0
#endif

static void
audio_buffer_init(AudioBuffer *audio) {
    audio->left = NULL;
    audio->right = NULL;

    audio->frame_count = 0;
    audio->sample_rate = 0;
    audio->channel_count = 0;

    return;
}

static void
audio_buffer_destroy(AudioBuffer *audio) {
    int64 allocation_size;

    allocation_size = audio->frame_count*SIZEOF(*audio->left);
    if (audio->left) {
        free2(audio->left, allocation_size);
    }
    if (audio->right) {
        free2(audio->right, allocation_size);
    }
    audio_buffer_init(audio);

    return;
}

static bool
audio_run_process(int32 argc, char **argv) {
    Command command = {0};
    bool result;

    command_push_array(&command, argc, argv);
    result = command_run_capture_all(&command);
    if (result) {
        result = command.result.exited
                 && (command.result.exit_status == 0);
    }
    command_free(&command);

    return result;
}

static bool
audio_check_ffmpeg(char *ffmpeg_path) {
    char *argv[] = {
        ffmpeg_path,
        "-hide_banner",
        "-version",
        NULL,
    };

    return audio_run_process(LENGTH(argv) - 1, argv);
}

static bool
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
        NULL,
    };

    return audio_run_process(LENGTH(argv) - 1, argv);
}

static bool
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
        NULL,
    };
    Command command = {0};
    float *samples;
    bool result = false;
    int64 frame_bytes;
    int64 raw_len;
    int64 sample_count;

    audio_buffer_destroy(audio);
    command_push_array(&command, LENGTH(argv) - 1, argv);
    if (!command_run_capture_all(&command)) {
        goto cleanup;
    }
    if (!command.result.exited || (command.result.exit_status != 0)) {
        goto cleanup;
    }

    raw_len = command.result.stdout_len;
    frame_bytes = 2*SIZEOF(*samples);
    if ((raw_len % frame_bytes) != 0) {
        goto cleanup;
    }

    audio->frame_count = raw_len/frame_bytes;
    audio->sample_rate = 44100;
    audio->channel_count = 2;
    if (audio->frame_count == 0) {
        result = true;
        goto cleanup;
    }

    sample_count = audio->frame_count*SIZEOF(*audio->left);
    audio->left = malloc2(sample_count);
    audio->right = malloc2(sample_count);

    samples = (float *)command.result.stdout_output;
    for (int64 i = 0; i < audio->frame_count; i += 1) {
        audio->left[i] = samples[2*i];
        audio->right[i] = samples[2*i + 1];
    }
    result = true;

cleanup:
    command_free(&command);
    if (!result) {
        audio_buffer_destroy(audio);
    }

    return result;
}

static bool
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
        NULL,
    };
    Command command = {0};
    float *interleaved;
    int32 null_fd;
    int32 pipe_fds[2];
    int32 status;
    int64 frame_capacity;
    int64 frame_offset;
    pid_t pid;
    pid_t waited;
    void (*previous_sigpipe)(int);

    if ((audio == NULL) || (path == NULL) || (format == NULL)
        || (ffmpeg_path == NULL)) {
        return false;
    }
    if ((audio->frame_count < 0) || (audio->sample_rate != 44100)
        || (audio->channel_count != 2)) {
        return false;
    }
    if ((audio->frame_count > 0)
        && ((audio->left == NULL) || (audio->right == NULL))) {
        return false;
    }

    command_push_array(&command, LENGTH(argv) - 1, argv);
    frame_capacity = 4096;
    interleaved = malloc2(2*frame_capacity*SIZEOF(*interleaved));

    if (pipe(pipe_fds) != 0) {
        free2(interleaved, 2*frame_capacity*SIZEOF(*interleaved));
        command_free(&command);
        return false;
    }

    switch (pid = fork()) {
    case -1:
        free2(interleaved, 2*frame_capacity*SIZEOF(*interleaved));
        command_free(&command);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return false;
    case 0:
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

        command_child_exec(&command, COMMAND_NONE, NULL, NULL);
    default:
        break;
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
        byte_count = 2*chunk_frames*SIZEOF(*interleaved);
        while (byte_offset < byte_count) {
            int64 bytes_written;

            bytes_written = write64(pipe_fds[1],
                                    bytes + byte_offset,
                                    byte_count - byte_offset);
            if (bytes_written > 0) {
                byte_offset += (int64)bytes_written;
                continue;
            }
            if ((bytes_written < 0) && (errno == EINTR)) {
                continue;
            }

            close(pipe_fds[1]);
            signal(SIGPIPE, previous_sigpipe);
            free2(interleaved, 2*frame_capacity*SIZEOF(*interleaved));
            waitpid(pid, &status, 0);
            command_free(&command);
            return false;
        }

        frame_offset += chunk_frames;
    }

    close(pipe_fds[1]);
    signal(SIGPIPE, previous_sigpipe);
    free2(interleaved, 2*frame_capacity*SIZEOF(*interleaved));

    do {
        waited = waitpid(pid, &status, 0);
    } while ((waited < 0) && (errno == EINTR));

    command_free(&command);
    if (waited < 0) {
        return false;
    }
    if (!WIFEXITED(status)) {
        return false;
    }

    return WEXITSTATUS(status) == 0;
}

#if TESTING_audio

#define CBASE_IMPLEMENT
#include "cbase.h"

static int32
audio_test_fail(char *name) {
    error2("audio test failed: %s\n", name);

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
    int32 fd;

    audio_buffer_init(&audio);
    if (audio.left || audio.right) {
        exit(audio_test_fail("buffer pointers"));
    }
    if (audio.frame_count != 0) {
        exit(audio_test_fail("frame count"));
    }
    audio_buffer_destroy(&audio);

    if (audio_check_ffmpeg("/definitely/missing/ffmpeg")) {
        exit(audio_test_fail("missing ffmpeg accepted"));
    }
    if (audio_can_decode_file("missing.wav",
                              "/definitely/missing/ffmpeg")) {
        exit(audio_test_fail("missing ffmpeg decode accepted"));
    }

    if (SNPRINTF(script,
                 "/tmp/uvr_fake_ffmpeg_%lld",
                 (int64)getpid()) < 0) {
        exit(audio_test_fail("fake ffmpeg path"));
    }
    if (SNPRINTF(output,
                 "/tmp/uvr_fake_audio_%lld",
                 (int64)getpid()) < 0) {
        exit(audio_test_fail("fake output path"));
    }
    fd = open(script, O_WRONLY|O_CREAT|O_EXCL, 0700);
    if (fd < 0) {
        exit(audio_test_fail("fake ffmpeg file"));
    }
    if (write64(fd, script_text, SIZEOF(script_text) - 1)
        != SIZEOF(script_text) - 1) {
        close(fd);
        unlink(script);
        exit(audio_test_fail("fake ffmpeg write"));
    }
    close(fd);
    if (chmod(script, 0700) != 0) {
        unlink(script);
        exit(audio_test_fail("fake ffmpeg chmod"));
    }

    if (!audio_check_ffmpeg(script)) {
        unlink(script);
        exit(audio_test_fail("fake ffmpeg version"));
    }
    if (!audio_can_decode_file("input.mp3", script)) {
        unlink(script);
        exit(audio_test_fail("fake ffmpeg decode check"));
    }
    if (!audio_read_file(&audio, "input.mp3", script)) {
        unlink(script);
        exit(audio_test_fail("read fake audio"));
    }

    if (audio.sample_rate != 44100) {
        audio_buffer_destroy(&audio);
        exit(audio_test_fail("sample rate"));
    }
    if (audio.channel_count != 2) {
        audio_buffer_destroy(&audio);
        exit(audio_test_fail("channels"));
    }
    if (audio.frame_count != 2) {
        audio_buffer_destroy(&audio);
        exit(audio_test_fail("decoded frame count"));
    }
    if ((audio.left[0] != 1.0f) || (audio.right[0] != -2.0f)) {
        audio_buffer_destroy(&audio);
        exit(audio_test_fail("decoded first frame"));
    }
    if ((audio.left[1] != 3.5f) || (audio.right[1] != -4.25f)) {
        audio_buffer_destroy(&audio);
        exit(audio_test_fail("decoded second frame"));
    }

    unlink(output);
    if (!audio_write_file(&audio, output, "wav", script)) {
        audio_buffer_destroy(&audio);
        unlink(script);
        exit(audio_test_fail("write fake audio"));
    }
    fd = open(output, O_RDONLY);
    if (fd < 0) {
        audio_buffer_destroy(&audio);
        unlink(script);
        exit(audio_test_fail("open fake output"));
    }
    if (read64(fd, output_raw, SIZEOF(output_raw))
        != SIZEOF(output_raw)) {
        close(fd);
        audio_buffer_destroy(&audio);
        unlink(script);
        unlink(output);
        exit(audio_test_fail("read fake output"));
    }
    close(fd);
    for (int32 i = 0; i < (int32)SIZEOF(output_raw); i += 1) {
        if (output_raw[i] != expected_raw[i]) {
            audio_buffer_destroy(&audio);
            unlink(script);
            unlink(output);
            exit(audio_test_fail("output interleaving"));
        }
    }

    audio_buffer_destroy(&audio);
    unlink(script);
    unlink(output);

    exit(0);
}

#endif /* TESTING_audio */
