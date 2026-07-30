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


static void
audio_compare_options_init(AudioCompareOptions *options) {
    options->mode = AUDIO_COMPARE_MODE_TOLERANT;

    options->max_offset_frames = 0;
    options->max_length_delta_frames = 0;

    options->max_abs_error = 0.0001f;
    options->max_rms_error = 0.00001f;
    options->min_snr_db = 80.0;

    return;
}

static void
audio_compare_result_init(AudioCompareResult *result) {
    result->mode = AUDIO_COMPARE_MODE_TOLERANT;

    result->decoded = false;
    result->valid = false;
    result->finite = false;
    result->length_ok = false;
    result->passed = false;

    result->expected_frames = 0;
    result->actual_frames = 0;
    result->compared_frames = 0;
    result->compared_samples = 0;
    result->length_delta_frames = 0;
    result->best_offset_frames = 0;
    result->nan_samples = 0;
    result->infinite_samples = 0;

    result->max_abs_error = 0.0f;
    result->rms_error = 0.0f;
    result->expected_peak = 0.0f;
    result->actual_peak = 0.0f;
    result->snr_db = 0.0;

    return;
}

static int64
audio_int64_abs(int64 value) {
    if (value < 0) {
        return -value;
    }

    return value;
}

static float
audio_float_abs(float value) {
    if (value < 0.0f) {
        return -value;
    }

    return value;
}

static bool
audio_buffer_valid(AudioBuffer *audio) {
    if (audio == NULL) {
        return false;
    }
    if ((audio->sample_rate != 44100) || (audio->channel_count != 2)) {
        return false;
    }
    if (audio->frame_count < 0) {
        return false;
    }
    if ((audio->frame_count > 0)
        && ((audio->left == NULL) || (audio->right == NULL))) {
        return false;
    }

    return true;
}

static bool
audio_compare_measure_offset(
    AudioCompareResult *result,
    AudioBuffer *expected,
    AudioBuffer *actual,
    int64 offset_frames
) {
    double error_sum;
    double signal_sum;
    int64 expected_start;
    int64 actual_start;
    int64 frame_count;

    audio_compare_result_init(result);
    result->valid = true;
    result->decoded = true;
    result->finite = true;
    result->expected_frames = expected->frame_count;
    result->actual_frames = actual->frame_count;
    result->length_delta_frames = audio_int64_abs(expected->frame_count
                                                  - actual->frame_count);
    result->best_offset_frames = offset_frames;

    expected_start = 0;
    actual_start = 0;
    if (offset_frames > 0) {
        actual_start = offset_frames;
    } else {
        expected_start = -offset_frames;
    }
    if ((expected_start > expected->frame_count)
        || (actual_start > actual->frame_count)) {
        return false;
    }

    frame_count = expected->frame_count - expected_start;
    if ((actual->frame_count - actual_start) < frame_count) {
        frame_count = actual->frame_count - actual_start;
    }
    if (frame_count < 0) {
        return false;
    }

    result->compared_frames = frame_count;
    result->compared_samples = 2*frame_count;
    if (frame_count == 0) {
        result->snr_db = 999.0;
        return true;
    }

    error_sum = 0.0;
    signal_sum = 0.0;
    for (int64 i = 0; i < frame_count; i += 1) {
        float expected_samples[2];
        float actual_samples[2];

        expected_samples[0] = expected->left[expected_start + i];
        expected_samples[1] = expected->right[expected_start + i];
        actual_samples[0] = actual->left[actual_start + i];
        actual_samples[1] = actual->right[actual_start + i];

        for (int32 channel = 0; channel < 2; channel += 1) {
            double error;
            float actual_abs;
            float expected_abs;

            if (isnan(expected_samples[channel])
                || isnan(actual_samples[channel])) {
                result->nan_samples += 1;
                result->finite = false;
                continue;
            }
            if (isinf(expected_samples[channel])
                || isinf(actual_samples[channel])) {
                result->infinite_samples += 1;
                result->finite = false;
                continue;
            }

            expected_abs = audio_float_abs(expected_samples[channel]);
            actual_abs = audio_float_abs(actual_samples[channel]);
            if (expected_abs > result->expected_peak) {
                result->expected_peak = expected_abs;
            }
            if (actual_abs > result->actual_peak) {
                result->actual_peak = actual_abs;
            }

            error = (double)expected_samples[channel]
                    - (double)actual_samples[channel];
            error_sum += error*error;
            signal_sum += (double)expected_samples[channel]
                          *(double)expected_samples[channel];
            if ((float)fabs(error) > result->max_abs_error) {
                result->max_abs_error = (float)fabs(error);
            }
        }
    }

    if (result->compared_samples > 0) {
        result->rms_error = (float)sqrt(error_sum
                                        /(double)result->compared_samples);
    }
    if (error_sum == 0.0) {
        result->snr_db = 999.0;
    } else if (signal_sum == 0.0) {
        result->snr_db = -999.0;
    } else {
        result->snr_db = 10.0*log10(signal_sum/error_sum);
    }

    return true;
}

static bool
audio_compare_better_result(
    AudioCompareResult *candidate,
    AudioCompareResult *best,
    bool have_best
) {
    if (!have_best) {
        return true;
    }
    if (candidate->rms_error < best->rms_error) {
        return true;
    }
    if ((candidate->rms_error == best->rms_error)
        && (candidate->max_abs_error < best->max_abs_error)) {
        return true;
    }

    return false;
}

static bool
audio_compare_result_passes(
    AudioCompareResult *result,
    AudioCompareOptions *options
) {
    if (!result->valid || !result->decoded || !result->finite) {
        return false;
    }
    if (!result->length_ok) {
        return false;
    }
    if (options->mode == AUDIO_COMPARE_MODE_STRICT) {
        return (result->max_abs_error == 0.0f)
               && (result->rms_error == 0.0f);
    }
    if (options->mode == AUDIO_COMPARE_MODE_SNR) {
        return result->snr_db >= options->min_snr_db;
    }

    return (result->max_abs_error <= options->max_abs_error)
           && (result->rms_error <= options->max_rms_error);
}

static bool
audio_compare_buffers(
    AudioCompareResult *result,
    AudioBuffer *expected,
    AudioBuffer *actual,
    AudioCompareOptions *options
) {
    AudioCompareOptions default_options;
    AudioCompareResult best_result;
    int64 max_offset;
    bool have_best;

    if (result == NULL) {
        return false;
    }

    audio_compare_result_init(result);
    if (options == NULL) {
        audio_compare_options_init(&default_options);
        options = &default_options;
    }
    result->mode = options->mode;

    if (!audio_buffer_valid(expected) || !audio_buffer_valid(actual)) {
        return false;
    }

    result->valid = true;
    result->decoded = true;
    result->finite = true;
    result->expected_frames = expected->frame_count;
    result->actual_frames = actual->frame_count;
    result->length_delta_frames = audio_int64_abs(expected->frame_count
                                                  - actual->frame_count);
    result->length_ok = result->length_delta_frames
                        <= options->max_length_delta_frames;

    max_offset = 0;
    if (options->mode == AUDIO_COMPARE_MODE_OFFSET_TOLERANT) {
        max_offset = options->max_offset_frames;
        if (max_offset < 0) {
            max_offset = 0;
        }
    }

    have_best = false;
    for (int64 offset = -max_offset; offset <= max_offset; offset += 1) {
        AudioCompareResult candidate;

        if (!audio_compare_measure_offset(&candidate,
                                          expected,
                                          actual,
                                          offset)) {
            continue;
        }
        candidate.mode = options->mode;
        candidate.length_ok = result->length_ok;
        if (audio_compare_better_result(&candidate,
                                        &best_result,
                                        have_best)) {
            best_result = candidate;
            have_best = true;
        }
    }

    if (!have_best) {
        return false;
    }

    *result = best_result;
    result->passed = audio_compare_result_passes(result, options);

    return result->passed;
}

static bool
audio_compare_files(
    AudioCompareResult *result,
    char *expected_path,
    char *actual_path,
    AudioCompareOptions *options,
    char *ffmpeg_path
) {
    AudioBuffer expected;
    AudioBuffer actual;
    bool ok;

    if (result == NULL) {
        return false;
    }

    audio_compare_result_init(result);
    audio_buffer_init(&expected);
    audio_buffer_init(&actual);
    if ((expected_path == NULL) || (actual_path == NULL)
        || (ffmpeg_path == NULL)) {
        return false;
    }

    ok = false;
    if (!audio_read_file(&expected, expected_path, ffmpeg_path)) {
        goto cleanup;
    }
    if (!audio_read_file(&actual, actual_path, ffmpeg_path)) {
        goto cleanup;
    }

    result->decoded = true;
    ok = audio_compare_buffers(result, &expected, &actual, options);

cleanup:
    audio_buffer_destroy(&actual);
    audio_buffer_destroy(&expected);

    return ok;
}

static bool
audio_compare_reconstruction_buffers(
    AudioCompareResult *result,
    AudioBuffer *mixture,
    AudioBuffer *first_stem,
    AudioBuffer *second_stem,
    AudioCompareOptions *options
) {
    AudioBuffer reconstructed;
    bool ok;

    if (result == NULL) {
        return false;
    }

    audio_compare_result_init(result);
    if (!audio_buffer_valid(mixture) || !audio_buffer_valid(first_stem)
        || !audio_buffer_valid(second_stem)) {
        return false;
    }
    if ((first_stem->frame_count != mixture->frame_count)
        || (second_stem->frame_count != mixture->frame_count)) {
        result->valid = true;
        result->expected_frames = mixture->frame_count;
        result->actual_frames = first_stem->frame_count;
        return false;
    }

    audio_buffer_init(&reconstructed);
    reconstructed.frame_count = mixture->frame_count;
    reconstructed.sample_rate = mixture->sample_rate;
    reconstructed.channel_count = mixture->channel_count;
    if (reconstructed.frame_count > 0) {
        reconstructed.left = malloc2(reconstructed.frame_count
                                     *SIZEOF(*reconstructed.left));
        reconstructed.right = malloc2(reconstructed.frame_count
                                      *SIZEOF(*reconstructed.right));
    }

    for (int64 i = 0; i < reconstructed.frame_count; i += 1) {
        reconstructed.left[i] = first_stem->left[i] + second_stem->left[i];
        reconstructed.right[i] = first_stem->right[i] + second_stem->right[i];
    }

    ok = audio_compare_buffers(result, mixture, &reconstructed, options);
    audio_buffer_destroy(&reconstructed);

    return ok;
}

static void
audio_compare_result_print(
    AudioCompareResult *result,
    char *name
) {
    char *label;
    char *mode;

    label = name;
    if (label == NULL) {
        label = "audio";
    }
    if (result == NULL) {
        error2("%s comparison result is unavailable\n", label);
        return;
    }

    mode = AUDIO_COMPARE_MODE_str(result->mode);
    error2(
        "%s: passed=%d mode=%s expected_frames=%lld "
        "actual_frames=%lld delta=%lld compared_frames=%lld "
        "offset=%lld max_abs=%g rms=%g snr_db=%.2f "
        "expected_peak=%g actual_peak=%g nan=%lld inf=%lld\n",
        label,
        result->passed,
        mode,
        result->expected_frames,
        result->actual_frames,
        result->length_delta_frames,
        result->compared_frames,
        result->best_offset_frames,
        (double)result->max_abs_error,
        (double)result->rms_error,
        result->snr_db,
        (double)result->expected_peak,
        (double)result->actual_peak,
        result->nan_samples,
        result->infinite_samples);
    AUDIO_COMPARE_MODE_str_free(mode);

    return;
}

#if TESTING_audio

#define CBASE_IMPLEMENT
#include "cbase.h"

static int32
audio_test_fail(char *name) {
    error2("audio test failed: %s\n", name);

    return 1;
}

static void
audio_test_buffer(
    AudioBuffer *audio,
    float *left,
    float *right,
    int64 frame_count
) {
    audio->left = left;
    audio->right = right;

    audio->frame_count = frame_count;
    audio->sample_rate = 44100;
    audio->channel_count = 2;

    return;
}

static int32
audio_test_compare_helpers(void) {
    AudioBuffer actual;
    AudioBuffer expected;
    AudioBuffer mixture;
    AudioBuffer stem_a;
    AudioBuffer stem_b;
    AudioCompareOptions options;
    AudioCompareResult result;
    char *mode_name;
    float actual_left[] = {0.0f, 0.25f, -0.5f, 0.75f};
    float actual_right[] = {1.0f, -1.0f, 0.5f, -0.25f};
    float expected_left[] = {0.0f, 0.25f, -0.5f, 0.75f};
    float expected_right[] = {1.0f, -1.0f, 0.5f, -0.25f};
    float shifted_left[] = {9.0f, 0.0f, 0.25f, -0.5f};
    float shifted_right[] = {9.0f, 1.0f, -1.0f, 0.5f};
    float mixture_left[] = {1.0f, 0.5f, -0.25f};
    float mixture_right[] = {-0.5f, 0.75f, 0.125f};
    float stem_a_left[] = {0.25f, 0.25f, -0.125f};
    float stem_a_right[] = {-0.25f, 0.25f, 0.5f};
    float stem_b_left[] = {0.75f, 0.25f, -0.125f};
    float stem_b_right[] = {-0.25f, 0.5f, -0.375f};

    audio_test_buffer(&expected,
                      expected_left,
                      expected_right,
                      LENGTH(expected_left));
    audio_test_buffer(&actual,
                      actual_left,
                      actual_right,
                      LENGTH(actual_left));

    mode_name = AUDIO_COMPARE_MODE_str(AUDIO_COMPARE_MODE_OFFSET_TOLERANT);
    if (!strequal(mode_name, "AUDIO_COMPARE_MODE_OFFSET_TOLERANT")) {
        AUDIO_COMPARE_MODE_str_free(mode_name);
        return audio_test_fail("compare mode string");
    }
    AUDIO_COMPARE_MODE_str_free(mode_name);
    if (AUDIO_COMPARE_MODE_parse("SNR") != AUDIO_COMPARE_MODE_SNR) {
        return audio_test_fail("compare mode parse");
    }

    audio_compare_options_init(&options);
    options.mode = AUDIO_COMPARE_MODE_STRICT;
    if (!audio_compare_buffers(&result, &expected, &actual, &options)) {
        audio_compare_result_print(&result, "strict equal");
        return audio_test_fail("strict compare equal buffers");
    }
    if (result.max_abs_error != 0.0f) {
        audio_compare_result_print(&result, "strict metric");
        return audio_test_fail("strict compare metric");
    }

    actual_left[1] += 0.000001f;
    audio_compare_options_init(&options);
    if (!audio_compare_buffers(&result, &expected, &actual, &options)) {
        audio_compare_result_print(&result, "tolerant close");
        return audio_test_fail("tolerant compare close buffers");
    }

    actual_left[1] += 0.01f;
    if (audio_compare_buffers(&result, &expected, &actual, &options)) {
        audio_compare_result_print(&result, "tolerant far");
        return audio_test_fail("tolerant compare far buffers");
    }
    actual_left[1] = expected_left[1];

    audio_test_buffer(&actual,
                      shifted_left,
                      shifted_right,
                      LENGTH(shifted_left));
    audio_compare_options_init(&options);
    options.mode = AUDIO_COMPARE_MODE_OFFSET_TOLERANT;
    options.max_offset_frames = 1;
    if (!audio_compare_buffers(&result, &expected, &actual, &options)) {
        audio_compare_result_print(&result, "offset");
        return audio_test_fail("offset compare shifted buffers");
    }
    if (result.best_offset_frames != 1) {
        audio_compare_result_print(&result, "offset metric");
        return audio_test_fail("offset compare selected offset");
    }

    audio_test_buffer(&mixture,
                      mixture_left,
                      mixture_right,
                      LENGTH(mixture_left));
    audio_test_buffer(&stem_a,
                      stem_a_left,
                      stem_a_right,
                      LENGTH(stem_a_left));
    audio_test_buffer(&stem_b,
                      stem_b_left,
                      stem_b_right,
                      LENGTH(stem_b_left));
    audio_compare_options_init(&options);
    options.mode = AUDIO_COMPARE_MODE_STRICT;
    if (!audio_compare_reconstruction_buffers(&result,
                                               &mixture,
                                               &stem_a,
                                               &stem_b,
                                               &options)) {
        audio_compare_result_print(&result, "reconstruction");
        return audio_test_fail("reconstruction compare");
    }

    audio_test_buffer(&actual,
                      actual_left,
                      actual_right,
                      LENGTH(actual_left));
    audio_compare_options_init(&options);
    options.mode = AUDIO_COMPARE_MODE_SNR;
    options.min_snr_db = 200.0;
    if (!audio_compare_buffers(&result, &expected, &actual, &options)) {
        audio_compare_result_print(&result, "snr equal");
        return audio_test_fail("snr compare equal buffers");
    }

    return 0;
}

int
main(void) {
    AudioBuffer audio;
    AudioCompareOptions compare_options;
    AudioCompareResult compare_result;
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

    if (audio_test_compare_helpers() != 0) {
        exit(1);
    }

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
    audio_compare_options_init(&compare_options);
    if (!audio_compare_files(&compare_result,
                             "expected.flac",
                             "actual.wav",
                             &compare_options,
                             script)) {
        audio_compare_result_print(&compare_result, "file compare");
        unlink(script);
        exit(audio_test_fail("compare fake audio files"));
    }
    if (compare_result.expected_frames != 2) {
        audio_compare_result_print(&compare_result, "file compare frames");
        unlink(script);
        exit(audio_test_fail("compare fake audio frames"));
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
