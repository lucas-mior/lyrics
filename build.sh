#!/bin/sh -e

# shellcheck disable=SC2086

set -eu

if [ -n "${BASH_VERSION:-}" ]; then
    # shellcheck disable=SC3044
    shopt -s expand_aliases
fi

alias trace_on='set -x'
alias trace_off='{ set +x; } 2>/dev/null'

dir=$(dirname "$(readlink -f "$0")")
cd "$dir" || exit

target=${1:-build}
target_arg=${2:-}

APP=${APP:-${target_arg:-all}}
GET_VOICE_PROGRAM=${GET_VOICE_PROGRAM:-bin/get_voice}
GEN_LRC_RAW_PROGRAM=${GEN_LRC_RAW_PROGRAM:-bin/gen_lrc_raw}
GEN_LRC_PROGRAM=${GEN_LRC_PROGRAM:-bin/gen_lrc}
MODEL=${MODEL:-models/identity.onnx}
INPUT=${INPUT:-song.mp3}
OUTPUT=${OUTPUT:-vocals.wav}
DEFAULT_LDLIBS=${DEFAULT_LDLIBS:-"-lm"}

requested_cc=${CC:-}
if [ "$target" = "test" ] \
   && [ -z "$requested_cc" ] \
   && command -v tcc >/dev/null 2>&1; then
    CC=tcc
else
    CC=${requested_cc:-cc}
fi

CPPFLAGS="${CPPFLAGS:-}"
CFLAGS="${CFLAGS:-}"
LDFLAGS="${LDFLAGS:-}"

CPPFLAGS="$CPPFLAGS -D_DEFAULT_SOURCE"
CPPFLAGS="$CPPFLAGS -Icbase -I. -Isrc"

CFLAGS="$CFLAGS -std=c11"
CFLAGS="$CFLAGS -Wfatal-errors"
CFLAGS="$CFLAGS -Wextra -Wall"
CFLAGS="$CFLAGS -Wno-format-pedantic"
CFLAGS="$CFLAGS -Wno-unknown-warning-option"
CFLAGS="$CFLAGS -Wno-gnu-union-cast"
CFLAGS="$CFLAGS -Wno-unused-macros"
CFLAGS="$CFLAGS -Wno-constant-logical-operand"
CFLAGS="$CFLAGS -Wno-float-equal"
CFLAGS="$CFLAGS -Wno-undefined-internal"
CFLAGS="$CFLAGS -Wno-cast-qual"
CFLAGS="$CFLAGS -Wno-unknown-pragmas"
CFLAGS="$CFLAGS -Wno-char-subscripts"
CFLAGS="$CFLAGS -Wno-padded"
CFLAGS="$CFLAGS -Wno-unused-function"
CFLAGS="$CFLAGS -Wno-reserved-identifier"
CFLAGS="$CFLAGS -Wno-documentation"

OS=$(uname -a)
GNUSOURCE=
if echo "$OS" | grep -q "Linux"; then
    if echo "$OS" | grep -q "GNU"; then
        GNUSOURCE="-D_GNU_SOURCE"
    fi
fi

case "$OS" in
*Linux*)
    CPPFLAGS="$CPPFLAGS -D_XOPEN_SOURCE=700"
    ;;
*Darwin*)
    CPPFLAGS="$CPPFLAGS -D_XOPEN_SOURCE=700 -D_DARWIN_C_SOURCE"
    ;;
esac

case "$CC" in
clang|*/clang)
    CFLAGS="$CFLAGS -Weverything"
    CFLAGS="$CFLAGS -Wno-unsafe-buffer-usage"
    CFLAGS="$CFLAGS -Wno-format-nonliteral"
    CFLAGS="$CFLAGS -Wno-disabled-macro-expansion"
    CFLAGS="$CFLAGS -Wno-c++-keyword"
    CFLAGS="$CFLAGS -Wno-pre-c11-compat"
    CFLAGS="$CFLAGS -Wno-implicit-void-ptr-cast"
    CFLAGS="$CFLAGS -Wno-ignored-attributes"
    CFLAGS="$CFLAGS -Wno-covered-switch-default"
    CFLAGS="$CFLAGS -Wno-used-but-marked-unused"
    CFLAGS="$CFLAGS -Wno-implicit-int-enum-cast"
    CFLAGS="$CFLAGS -Wno-assign-enum"
    CFLAGS="$CFLAGS -Wno-cast-function-type-strict"
    CFLAGS="$CFLAGS -Wno-bad-function-cast"
    CFLAGS="$CFLAGS -Wno-fixed-enum-extension"
    CFLAGS="$CFLAGS -Wno-char-subscripts"
    ;;
esac

case "$target" in
build|all|run)
    CFLAGS="$CFLAGS $GNUSOURCE -O2 -g"
    ;;
debug)
    CFLAGS="$CFLAGS $GNUSOURCE -g3 -O0 -fsanitize=undefined"
    CPPFLAGS="$CPPFLAGS -DDEBUGGING=1 -Wno-unused-function"
    ;;
test)
    CFLAGS="$CFLAGS $GNUSOURCE -g3 -O0 -DDEBUGGING=1"
    CFLAGS="$CFLAGS -Wno-unused-function -Wno-unused-variable"
    if [ "$CC" != "tcc" ]; then
        CFLAGS="$CFLAGS -fsanitize=undefined -Wno-address"
    fi
    ;;
check)
    ;;
*)
    ;;
esac

if command -v xsel >/dev/null 2>&1; then
    xsel="xsel"
else
    xsel="cat"
fi

pkg_config_flags=

usage() {
    cat <<'USAGE'
usage: ./build.sh [command] [app-or-test]

commands:
    build    build all executables, or one selected app
    run      build and run one selected app
    test     build and run embedded module tests
    debug    build with debug flags and UBSan
    check    build with GCC and Clang static analyzers
    clean    remove generated build outputs
    help     show this message

environment:
    CC                   C compiler, default: cc or tcc for tests
    APP                  app to build/run, default: all for build
    GET_VOICE_PROGRAM    get_voice executable, default: bin/get_voice
    GEN_LRC_RAW_PROGRAM  raw LRC executable, default: bin/gen_lrc_raw
    GEN_LRC_PROGRAM      full LRC executable, default: bin/gen_lrc
    INPUT                run command input path, default: song.mp3
    OUTPUT               get_voice run output path, default: vocals.wav
    CFLAGS               extra compiler flags
    DEFAULT_LDLIBS       default libraries, default: -lm

available apps:
    all
    get_voice
    gen_lrc_raw
    gen_lrc
USAGE
}

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "missing required command: $1" >&2
        exit 1
    fi
}

pkg_config_add_flags() {
    pkg="$1"
    trace_on
    flags=$(pkg-config --cflags --libs "$pkg")
    trace_off

    pkg_config_flags="$pkg_config_flags $flags"
}

setup_pkg_config_flags() {
    require_command pkg-config

    pkg_config_add_flags libonnxruntime
    pkg_config_add_flags fftw3f
}

app_source() {
    case "$1" in
    get_voice)
        printf '%s\n' src/main_get_voice.c
        ;;
    gen_lrc_raw)
        printf '%s\n' src/main_gen_lrc_raw.c
        ;;
    gen_lrc)
        printf '%s\n' src/main_gen_lrc.c
        ;;
    *)
        echo "unknown app: $1" >&2
        echo "available apps: all get_voice gen_lrc_raw gen_lrc" >&2
        exit 1
        ;;
    esac
}

app_program() {
    case "$1" in
    get_voice)
        printf '%s\n' "$GET_VOICE_PROGRAM"
        ;;
    gen_lrc_raw)
        printf '%s\n' "$GEN_LRC_RAW_PROGRAM"
        ;;
    gen_lrc)
        printf '%s\n' "$GEN_LRC_PROGRAM"
        ;;
    *)
        echo "unknown app: $1" >&2
        echo "available apps: all get_voice gen_lrc_raw gen_lrc" >&2
        exit 1
        ;;
    esac
}

app_cppflags() {
    case "$1" in
    gen_lrc_raw|gen_lrc)
        printf '%s\n' "-DLRC_CTC_INFERENCE_ENABLE_ORT=1"
        ;;
    *)
        printf '%s\n' ""
        ;;
    esac
}

build_app() {
    app="$1"
    source=$(app_source "$app")
    output=$(app_program "$app")
    app_cppflags=$(app_cppflags "$app")

    mkdir -p "$(dirname "$output")"

    trace_on
    $CC $CPPFLAGS $app_cppflags $CFLAGS "$source" \
        $LDFLAGS $pkg_config_flags $DEFAULT_LDLIBS \
        -o "$output"
    trace_off
}

build_program() {
    setup_pkg_config_flags

    case "$APP" in
    all)
        build_app get_voice
        build_app gen_lrc_raw
        build_app gen_lrc
        ;;
    get_voice|gen_lrc_raw|gen_lrc)
        build_app "$APP"
        ;;
    *)
        echo "unknown app: $APP" >&2
        echo "available apps: all get_voice gen_lrc_raw gen_lrc" >&2
        exit 1
        ;;
    esac
}

module_test_name() {
    basename "$1" | sed 's/\.c$//'
}

module_test_flags() {
    awk '/\/\/ flags:/ { $1=$2=""; print $0 }' "$1"
}

run_check() {
    if [ -n "$target_arg" ]; then
        CC=gcc CFLAGS="-fanalyzer" "$0" build "$target_arg"
    else
        CC=gcc CFLAGS="-fanalyzer" "$0" build
    fi

    analyzer_flags="--analyze -Xanalyzer -analyzer-output=text"
    analyzer_flags="$analyzer_flags -Xanalyzer -analyzer-werror"
    analyzer_flags="$analyzer_flags -Xanalyzer -analyzer-opt-analyze-headers"
    analyzer_flags="$analyzer_flags -Wno-unused-command-line-argument"

    if [ -n "$target_arg" ]; then
        CC=clang CFLAGS="$analyzer_flags" "$0" build "$target_arg"
    else
        CC=clang CFLAGS="$analyzer_flags" "$0" build
    fi
}

run_tests() {
    setup_pkg_config_flags

    find src -iname "*.c" | grep -v '/main[^/]*\.c$' | sort \
        | while read -r module; do
        name=$(module_test_name "$module")
        test_exe="/tmp/${name}_test"

        if [ -n "$target_arg" ] \
           && [ "$target_arg" != "$name" ] \
           && [ "$target_arg" != "${name}.c" ]; then
            continue
        fi

        printf '\nTesting %s ...\n' "$module"

        flags=$(module_test_flags "$module")

        trace_on
        if $CC $CPPFLAGS $CFLAGS \
              "-DTESTING_$name=1" -DTESTING=1 "$module" \
              $LDFLAGS $pkg_config_flags $DEFAULT_LDLIBS $flags \
              -o "$test_exe"; then
            if ! "$test_exe"; then
                if command -v gdb >/dev/null 2>&1; then
                    gdb --quiet \
                        -ex run -ex backtrace -ex quit \
                        "$test_exe" 2>&1 | $xsel
                fi
                exit 1
            fi
        else
            exit 1
        fi
        trace_off
    done
}

case "$target" in
build|all)
    build_program
    ;;
run)
    if [ "$APP" = all ]; then
        APP=get_voice
    fi
    build_program
    output=$(app_program "$APP")
    trace_on
    case "$APP" in
    get_voice)
        "$output" -i "$INPUT" -o "$OUTPUT" -m "$MODEL"
        ;;
    gen_lrc_raw|gen_lrc)
        "$output"
        ;;
    esac
    trace_off
    ;;
test)
    run_tests
    ;;
debug)
    build_program
    ;;
check)
    run_check
    ;;
clean)
    rm -rf bin
    ;;
help|-h|--help)
    usage
    ;;
*)
    usage >&2
    exit 1
    ;;
esac
