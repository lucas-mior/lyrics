#if !defined(LRC_PROGRESS_IMPLEMENTED)
#define LRC_PROGRESS_IMPLEMENTED

#include "cbase.h"
#include "progress.h"

#if !defined(TESTING_progress)
#define TESTING_progress 0
#endif

#define LRC_PROGRESS_BAR_WIDTH 32

static int64
lrc_progress_clamp_current(int64 current, int64 total) {
    if (current < 0) {
        return 0;
    }
    if (current > total) {
        return total;
    }

    return current;
}

static int32
lrc_progress_percent(int64 current, int64 total) {
    double ratio;

    if (total <= 0) {
        return 100;
    }

    ratio = (double)current/(double)total;
    if (ratio < 0.0) {
        ratio = 0.0;
    }
    if (ratio > 1.0) {
        ratio = 1.0;
    }

    return (int32)(ratio*100.0 + 0.5);
}

static int32
lrc_progress_filled_width(LrcProgress *progress) {
    double ratio;

    if (progress->total <= 0) {
        return progress->width;
    }

    ratio = (double)progress->current/(double)progress->total;
    if (ratio < 0.0) {
        ratio = 0.0;
    }
    if (ratio > 1.0) {
        ratio = 1.0;
    }

    return (int32)(ratio*(double)progress->width + 0.5);
}

static void
lrc_progress_render(LrcProgress *progress, bool newline) {
    int32 filled;
    int32 percent;
    char *label;

    if ((progress == NULL) || !progress->enabled) {
        return;
    }

    progress->current = lrc_progress_clamp_current(progress->current,
                                                   progress->total);
    filled = lrc_progress_filled_width(progress);
    percent = lrc_progress_percent(progress->current, progress->total);
    label = progress->label;
    if (label == NULL) {
        label = "progress";
    }

    fprintf(stderr, "\r%-28s [", label);
    for (int32 i = 0; i < progress->width; i += 1) {
        if (i < filled) {
            fputc('#', stderr);
        } else {
            fputc('.', stderr);
        }
    }
    fprintf(stderr,
            "] %3d%% %lld/%lld",
            percent,
            progress->current,
            progress->total);

    if (newline) {
        fputc('\n', stderr);
    }
    fflush(stderr);

    progress->rendered = true;
    progress->last_percent = percent;

    return;
}

static void
lrc_progress_init(
    LrcProgress *progress,
    bool enabled,
    char *label,
    int64 total
) {
    if (progress == NULL) {
        return;
    }

    progress->label = label;
    progress->current = 0;
    progress->total = total;
    if (progress->total <= 0) {
        progress->total = 1;
    }
    progress->width = LRC_PROGRESS_BAR_WIDTH;
    progress->last_percent = -1;
    progress->enabled = enabled;
    progress->rendered = false;
    progress->finished = false;

    return;
}

static void
lrc_progress_begin(LrcProgress *progress) {
    if (progress == NULL) {
        return;
    }

    progress->current = 0;
    lrc_progress_render(progress, false);

    return;
}

static void
lrc_progress_update(LrcProgress *progress, int64 current) {
    if (progress == NULL) {
        return;
    }
    if (progress->finished) {
        return;
    }

    progress->current = current;
    lrc_progress_render(progress, false);

    return;
}

static void
lrc_progress_finish(LrcProgress *progress) {
    if (progress == NULL) {
        return;
    }
    if (progress->finished) {
        return;
    }

    progress->current = progress->total;
    lrc_progress_render(progress, true);
    progress->finished = true;

    return;
}

static void
lrc_progress_cancel(LrcProgress *progress) {
    if (progress == NULL) {
        return;
    }
    if (!progress->enabled || !progress->rendered || progress->finished) {
        return;
    }

    fputc('\n', stderr);
    fflush(stderr);
    progress->finished = true;

    return;
}

#if TESTING_progress
int32
main(void) {
    LrcProgress progress;

    lrc_progress_init(&progress, false, "disabled", 3);
    lrc_progress_begin(&progress);
    lrc_progress_update(&progress, 1);
    lrc_progress_finish(&progress);

    lrc_progress_init(&progress, true, "progress test", 2);
    lrc_progress_begin(&progress);
    lrc_progress_update(&progress, 1);
    lrc_progress_finish(&progress);

    return 0;
}
#endif

#endif /* LRC_PROGRESS_IMPLEMENTED */
