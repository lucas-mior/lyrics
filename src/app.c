#include "app.h"

#include "audio.h"
#include "cli.h"
#include "fftw.h"
#include "ort.h"

#include <stdio.h>
#include <stdlib.h>

int32
app_run(int argc, char **argv) {
    CliOptions options;
    FftwRealPlan fftw_plan;
    OrtContext ort_context;
    OrtModel ort_model;
    FILE *model_file;
    int32 parse_result;
    int32 result;

    cli_options_init(&options);
    parse_result = cli_parse(&options, argc, argv);
    if (parse_result > 0) {
        return EXIT_SUCCESS;
    }
    if (parse_result < 0) {
        cli_print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    result = EXIT_FAILURE;
    fftw_real_plan_init_empty(&fftw_plan);
    ort_context_init_empty(&ort_context);
    ort_model_init_empty(&ort_model);

    if (!audio_check_ffmpeg(options.ffmpeg_path)) {
        fprintf(stderr, "could not run ffmpeg: %s\n", options.ffmpeg_path);
        fprintf(stderr, "set --ffmpeg to a valid FFmpeg executable\n");
        goto cleanup;
    }

    if (!audio_can_decode_file(options.input_path, options.ffmpeg_path)) {
        fprintf(stderr, "could not decode input with ffmpeg: %s\n",
                options.input_path);
        goto cleanup;
    }

    model_file = fopen(options.model_path, "rb");
    if (model_file == NULL) {
        fprintf(stderr, "could not read ONNX model: %s\n", options.model_path);
        goto cleanup;
    }
    fclose(model_file);

    if (!fftw_real_plan_init(&fftw_plan, options.n_fft)) {
        fprintf(stderr, "could not initialize FFTW plan for n_fft=%d\n",
                options.n_fft);
        goto cleanup;
    }

    if (!ort_context_init(&ort_context)) {
        fprintf(stderr, "could not initialize ONNX Runtime\n");
        goto cleanup;
    }

    if (!ort_model_load(&ort_context, &ort_model, options.model_path)) {
        fprintf(stderr, "could not load ONNX model: %s\n",
                options.model_path);
        goto cleanup;
    }

    cli_print_options(&options);
    fprintf(stderr, "audio extraction is not implemented yet\n");
    result = EXIT_SUCCESS;

cleanup:
    ort_model_destroy(&ort_context, &ort_model);
    ort_context_destroy(&ort_context);
    fftw_real_plan_destroy(&fftw_plan);

    return result;
}

#if TESTING_app

int
main(void) {
    return 0;
}

#endif /* TESTING_app */
