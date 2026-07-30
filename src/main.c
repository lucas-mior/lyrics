#if !defined(TESTING_app)
#define TESTING_app 0
#endif
#if !defined(TESTING_audio)
#define TESTING_audio 0
#endif
#if !defined(TESTING_cli)
#define TESTING_cli 0
#endif
#if !defined(TESTING_fftw)
#define TESTING_fftw 0
#endif
#if !defined(TESTING_mdx)
#define TESTING_mdx 0
#endif
#if !defined(TESTING_ort)
#define TESTING_ort 0
#endif
#if !defined(TESTING_stft)
#define TESTING_stft 0
#endif

#define UVR_TESTING \
    (TESTING_app || TESTING_audio || TESTING_cli || TESTING_fftw \
     || TESTING_mdx || TESTING_ort || TESTING_stft)

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define CBASE_API_DECL static
#define CBASE_API_DEF static
#define CBASE_IMPLEMENT
#include "cbase.h"

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#if UVR_TESTING && defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#include "fftw.c"
#include "stft.c"
#include "audio.c"
#include "ort.c"
#include "mdx.c"
#include "cli.c"
#include "app.c"

#if UVR_TESTING && defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#if !UVR_TESTING

int
main(int argc, char **argv) {
    exit(app_run((int32)argc, argv));
}

#endif /* !UVR_TESTING */
