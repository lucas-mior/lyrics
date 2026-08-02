#if !defined(LRC_PROGRESS_IMPLEMENTED)
#define LRC_PROGRESS_IMPLEMENTED

#include "cbase.h"
#include "lyricsync.h"
#include "progress.h"

#if !defined(TESTING_progress)
#define TESTING_progress 0
#endif

#define LRC_PROGRESS_LABEL_WIDTH 28
#define LRC_PROGRESS_MAX_LINE_WIDTH 80
#define LRC_PROGRESS_MIN_BAR_WIDTH 4

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
lrc_progress_decimal_digits(int64 value) {
    int32 digits = 1;

    if (value < 0) {
        digits += 1;
        value = -value;
    }

    while (value >= 10) {
        digits += 1;
        value /= 10;
    }

    return digits;
}

static int32
lrc_progress_terminal_columns(void) {
    int32 columns = LRC_PROGRESS_MAX_LINE_WIDTH;

#if OS_UNIX && defined(TIOCGWINSZ)
    struct winsize size;

    if (isatty(STDERR_FILENO)
        && (ioctl(STDERR_FILENO, TIOCGWINSZ, &size) == 0)
        && (size.ws_col > 0)
        && (size.ws_col <= LRC_PROGRESS_MAX_LINE_WIDTH)) {
        columns = (int32)size.ws_col;
    }
#endif

    if (columns > LRC_PROGRESS_MAX_LINE_WIDTH) {
        columns = LRC_PROGRESS_MAX_LINE_WIDTH;
    }

    return columns;
}

static int32
lrc_progress_bar_width(LrcProgress *progress) {
    int32 columns;
    int32 current_digits;
    int32 total_digits;
    int32 overhead;
    int32 width;

    columns = lrc_progress_terminal_columns();
    current_digits = lrc_progress_decimal_digits(progress->current);
    total_digits = lrc_progress_decimal_digits(progress->total);
    overhead = LRC_PROGRESS_LABEL_WIDTH + 10 + current_digits + total_digits;
    width = columns - overhead;
    if (width < LRC_PROGRESS_MIN_BAR_WIDTH) {
        width = LRC_PROGRESS_MIN_BAR_WIDTH;
    }

    return width;
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
lrc_progress_render(LrcProgress *progress) {
    int32 filled;
    int32 percent;
    char *label;

    if ((progress == NULL) || !progress->enabled) {
        return;
    }

    progress->current = lrc_progress_clamp_current(progress->current,
                                                   progress->total);
    percent = lrc_progress_percent(progress->current, progress->total);
    if ((percent == progress->last_percent)
        && (progress->current < progress->total)) {
        return;
    }

    progress->width = lrc_progress_bar_width(progress);
    filled = lrc_progress_filled_width(progress);
    label = progress->label;
    if (label == NULL) {
        label = "progress";
    }

    fprintf(stderr, "%-*.*s [",
            LRC_PROGRESS_LABEL_WIDTH,
            LRC_PROGRESS_LABEL_WIDTH,
            label);
    for (int32 i = 0; i < progress->width; i += 1) {
        if (i < filled) {
            fputc('#', stderr);
        } else {
            fputc('.', stderr);
        }
    }
    fprintf(stderr,
            "] %3d%% %lld/%lld\n",
            percent,
            progress->current,
            progress->total);
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
    progress->width = LRC_PROGRESS_MIN_BAR_WIDTH;
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
    lrc_progress_render(progress);

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
    lrc_progress_render(progress);

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
    lrc_progress_render(progress);
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

    progress->finished = true;

    return;
}

#if TESTING_progress
#define CBASE_IMPLEMENT
#include "cbase.h"

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
