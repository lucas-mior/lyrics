#if !defined(VOCALS_H)
#define VOCALS_H

#include "cbase.h"
#include "audio.h"
#include "mdx.h"

/*
 * Reusable phase-1 entry points.
 *
 * vocals_extract_audio() decodes a music file and returns the extracted stem in
 * memory. vocals_extract_file() is the executable-oriented wrapper that also
 * writes the stem to disk.
 */
typedef struct VocalsExtractionConfig {
    char *model_path;
    char *ffmpeg_path;

    MdxConfig mdx_config;

    bool print_info;
} VocalsExtractionConfig;

static void vocals_extraction_config_init(VocalsExtractionConfig *config);
static bool vocals_extract_audio(
    AudioBuffer *output_audio,
    char *input_path,
    VocalsExtractionConfig *config
);
static bool vocals_extract_file(
    VocalsExtractionConfig *config,
    char *input_path,
    char *output_path,
    char *container_format
);

#endif /* VOCALS_H */
