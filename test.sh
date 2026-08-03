#!/bin/sh

set -eu

script_dir=$(dirname "$(readlink -f "$0")")
cd "$script_dir" || exit 1

lyricsync=${LYRICSYNC:-bin/lyricsync}
samples_dir=${SAMPLES_DIR:-samples}
tolerance=${TOLERANCE_SECONDS:-0.5}
work_dir=$(mktemp -d "${TMPDIR:-/tmp}/lyricsync-test.XXXXXX")
keep_work_dir=0

cleanup() {
    if [ "$keep_work_dir" -eq 0 ]; then
        rm -rf "$work_dir"
    else
        printf 'Kept test outputs in %s\n' "$work_dir" >&2
    fi
}

trap cleanup EXIT INT HUP TERM

compare_lrc() {
    ref=$1
    actual=$2

    awk \
        -v tolerance="$tolerance" \
        -v ref_path="$ref" \
        -v actual_path="$actual" \
        '
function fail(message) {
    print message > "/dev/stderr"
    ok = 0
}

function parse_seconds(file, line_number, timestamp, parts) {
    if (split(timestamp, parts, ":") != 2) {
        fail(file ":" line_number ": invalid timestamp: " timestamp)
        return -1
    }
    if ((parts[1] !~ /^[0-9]+$/) ||
        (parts[2] !~ /^[0-9]+([.][0-9]+)?$/)) {
        fail(file ":" line_number ": invalid timestamp: " timestamp)
        return -1
    }

    return parts[1]*60 + parts[2]
}

function collect(file, line_number, line, seconds, text, close_index) {
    sub(/\r$/, "", line)
    close_index = index(line, "]")
    if ((substr(line, 1, 1) != "[") || (close_index <= 0)) {
        return
    }

    text = substr(line, close_index + 1)
    if (text == "") {
        return
    }

    seconds = parse_seconds(file,
                            line_number,
                            substr(line, 2, close_index - 2))
    if (seconds < 0) {
        return
    }

    if (file_index == 1) {
        ref_count += 1
        ref_seconds[ref_count] = seconds
        ref_text[ref_count] = text
        ref_line[ref_count] = line_number
    } else {
        actual_count += 1
        actual_seconds[actual_count] = seconds
        actual_text[actual_count] = text
        actual_line[actual_count] = line_number
    }
}

BEGIN {
    ok = 1
    file_index = 0
}

FNR == 1 {
    file_index += 1
}

file_index == 1 {
    collect(ref_path, FNR, $0)
    next
}

{
    collect(actual_path, FNR, $0)
}

END {
    if (actual_count != ref_count) {
        fail("non-empty lyric count mismatch: expected " ref_count \
             ", got " actual_count)
    }

    count = ref_count
    if (actual_count < count) {
        count = actual_count
    }

    for (i = 1; i <= count; i += 1) {
        if (actual_text[i] != ref_text[i]) {
            fail("lyric mismatch at non-empty lyric " i \
                 ": expected [" ref_line[i] "] " ref_text[i] \
                 ", got [" actual_line[i] "] " actual_text[i])
        }

        diff = actual_seconds[i] - ref_seconds[i]
        if (diff < 0) {
            diff = -diff
        }
        if (diff > tolerance) {
            fail("timestamp mismatch at non-empty lyric " i \
                 ": expected " ref_seconds[i] "s, got " \
                 actual_seconds[i] "s, diff " diff "s")
        }
    }

    exit ok ? 0 : 1
}
' "$ref" "$actual"
}

run_sample() {
    vocals=$1
    prefix=${vocals%.opus}
    lyrics=$prefix.txt
    ref=$prefix-ref.lrc
    name=$(basename "$prefix")
    actual=$work_dir/$name.lrc
    log=$work_dir/$name.log

    if [ ! -f "$lyrics" ]; then
        printf 'missing lyrics file for %s: %s\n' "$vocals" "$lyrics" >&2
        return 1
    fi
    if [ ! -f "$ref" ]; then
        printf 'missing reference LRC for %s: %s\n' "$vocals" "$ref" >&2
        return 1
    fi

    printf 'Testing %s ...\n' "$name"
    if ! "$lyricsync" \
            --input-vocals "$vocals" \
            --input-lyrics "$lyrics" \
            --output-lrc "$actual" >"$log" 2>&1; then
        printf 'lyricsync failed for %s\n' "$name" >&2
        cat "$log" >&2
        return 1
    fi

    if ! compare_lrc "$ref" "$actual"; then
        printf 'LRC mismatch for %s\n' "$name" >&2
        return 1
    fi

    printf 'PASS %s\n' "$name"

    return 0
}

if [ ! -x "$lyricsync" ]; then
    printf 'missing executable: %s\n' "$lyricsync" >&2
    printf 'build it first with: ./build.sh build\n' >&2
    exit 1
fi

sample_count=0
failure_count=0
for vocals in "$samples_dir"/*-vocals.opus; do
    if [ ! -f "$vocals" ]; then
        continue
    fi

    sample_count=$((sample_count + 1))
    if ! run_sample "$vocals"; then
        failure_count=$((failure_count + 1))
    fi

done

if [ "$sample_count" -eq 0 ]; then
    printf 'no sample vocals found in %s\n' "$samples_dir" >&2
    exit 1
fi

if [ "$failure_count" -ne 0 ]; then
    keep_work_dir=1
    printf '%s/%s sample tests failed\n' "$failure_count" "$sample_count" >&2
    exit 1
fi

printf 'PASS %s sample tests\n' "$sample_count"
