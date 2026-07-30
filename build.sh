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

script=$(basename "$0")
command=${1:-build}
test_filter=${2:-}

PYTHON=${PYTHON:-python3}
PKG_CONFIG=${PKG_CONFIG:-pkg-config}
ONNXRUNTIME_PKG_CONFIG_NAME=${ONNXRUNTIME_PKG_CONFIG_NAME:-onnxruntime}
FFTW_PKG_CONFIG_NAME=${FFTW_PKG_CONFIG_NAME:-fftw3f}

PROGRAM=${PROGRAM:-bin/uvr-c}
PROGRAM_SOURCE=${PROGRAM_SOURCE:-src/main.c}
TEST_MODULES=${TEST_MODULES:-}
MODEL=${MODEL:-models/identity.onnx}
INPUT=${INPUT:-song.mp3}
OUTPUT=${OUTPUT:-vocals.wav}

requested_cc=${CC:-}
if [ "$command" = "test" ] \
   && [ -z "$requested_cc" ] \
   && command -v tcc >/dev/null 2>&1; then
    CC=tcc
else
    CC=${requested_cc:-cc}
fi

CPPFLAGS="${CPPFLAGS:-}"
CFLAGS="${CFLAGS:-}"
LDFLAGS="${LDFLAGS:-}"
DEFAULT_LDLIBS=${DEFAULT_LDLIBS:-"-lm"}

CPPFLAGS="$CPPFLAGS -D_DEFAULT_SOURCE"
CPPFLAGS="$CPPFLAGS -Icbase -I. -Isrc"

CFLAGS="$CFLAGS -std=c11"
CFLAGS="$CFLAGS -Wfatal-errors"
CFLAGS="$CFLAGS -Wextra -Wall -Wvla"
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
    CFLAGS="$CFLAGS -Wno-char-subscripts"
    ;;
esac

case "$command" in
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
    CC=gcc
    CFLAGS="$CFLAGS $GNUSOURCE -DDEBUGGING=1 -fanalyzer"
    ;;
*)
    ;;
esac

if [ -d third_party/onnxruntime/lib/pkgconfig ]; then
    ort_pc=third_party/onnxruntime/lib/pkgconfig
    PKG_CONFIG_PATH="$ort_pc${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    export PKG_CONFIG_PATH
fi

if command -v xsel >/dev/null 2>&1; then
    xsel=xsel
else
    xsel=cat
fi

usage() {
    cat <<'USAGE'
usage: ./build.sh [command] [test]

commands:
    build    build the executable (default)
    run      build and run the CLI parser
    test     build and run embedded module tests
    debug    build with debug flags and UBSan
    check    build with GCC static analyzer
    model    regenerate models/identity.onnx
    setup    download ONNX Runtime and regenerate the identity model
    clean    remove generated build outputs
    help     show this message

environment:
    CC                           C compiler, default: cc or tcc for tests
    PYTHON                       Python interpreter, default: python3
    PKG_CONFIG                   pkg-config program, default: pkg-config
    ONNXRUNTIME_PKG_CONFIG_NAME  pkg-config package, default: onnxruntime
    FFTW_PKG_CONFIG_NAME         pkg-config package, default: fftw3f
    PROGRAM                      output executable, default: bin/uvr-c
    PROGRAM_SOURCE               unity source, default: src/main.c
    TEST_MODULES                 modules whose tests are run
    MODEL                        ONNX model path, default: models/identity.onnx
    INPUT                        run command input path, default: song.mp3
    OUTPUT                       run command output path, default: vocals.wav
    CFLAGS                       extra compiler flags
    DEFAULT_LDLIBS               default libraries, default: -lm
USAGE
}

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "missing required command: $1" >&2
        exit 1
    fi
}

resolve_onnxruntime_package() {
    require_command $PKG_CONFIG

    if $PKG_CONFIG --exists "$ONNXRUNTIME_PKG_CONFIG_NAME"; then
        printf '%s\n' "$ONNXRUNTIME_PKG_CONFIG_NAME"
        return 0
    fi

    if [ "$ONNXRUNTIME_PKG_CONFIG_NAME" = onnxruntime ] \
       && $PKG_CONFIG --exists libonnxruntime; then
        printf '%s\n' libonnxruntime
        return 0
    fi

    echo "pkg-config could not find $ONNXRUNTIME_PKG_CONFIG_NAME" >&2
    echo "run ./build.sh setup or set PKG_CONFIG_PATH" >&2
    echo "to the directory containing onnxruntime.pc" >&2
    exit 1
}

resolve_fftw_package() {
    require_command $PKG_CONFIG

    if $PKG_CONFIG --exists "$FFTW_PKG_CONFIG_NAME"; then
        printf '%s\n' "$FFTW_PKG_CONFIG_NAME"
        return 0
    fi

    if [ "$FFTW_PKG_CONFIG_NAME" = fftw3f ] \
       && $PKG_CONFIG --exists fftw3; then
        printf '%s\n' fftw3
        return 0
    fi

    printf '%s\n' ""
    return 0
}

module_needs_onnxruntime() {
    case "$1" in
    */mdx.c|*/ort.c)
        return 0
        ;;
    esac

    grep -Eq 'onnxruntime_c_api.h|"ort\\.c"|"ort\\.h"' "$1"
}

module_needs_fftw() {
    case "$1" in
    */fftw.c|*/mdx.c|*/stft.c)
        return 0
        ;;
    esac

    grep -Eq 'fftw3.h|"fftw\\.c"|"fftw\\.h"|"stft\\.c"|"stft\\.h"' "$1"
}

dependency_cflags() {
    ort_package=$(resolve_onnxruntime_package)
    fftw_package=$(resolve_fftw_package)

    if [ -n "$fftw_package" ]; then
        $PKG_CONFIG --cflags "$ort_package" "$fftw_package"
        return 0
    fi

    $PKG_CONFIG --cflags "$ort_package"
}

dependency_ldlibs() {
    ort_package=$(resolve_onnxruntime_package)
    fftw_package=$(resolve_fftw_package)

    if [ -n "$fftw_package" ]; then
        $PKG_CONFIG --libs "$ort_package" "$fftw_package"
        return 0
    fi

    $PKG_CONFIG --libs "$ort_package"
    printf ' %s\n' '-lfftw3f'
}

dependency_cflags_for_module() {
    module="$1"
    packages=""

    if module_needs_onnxruntime "$module"; then
        packages="$packages $(resolve_onnxruntime_package)"
    fi
    if module_needs_fftw "$module"; then
        fftw_package=$(resolve_fftw_package)
        if [ -n "$fftw_package" ]; then
            packages="$packages $fftw_package"
        fi
    fi

    if [ -n "$packages" ]; then
        $PKG_CONFIG --cflags $packages
    fi
}

dependency_ldlibs_for_module() {
    module="$1"
    packages=""
    libs=""

    if module_needs_onnxruntime "$module"; then
        packages="$packages $(resolve_onnxruntime_package)"
    fi
    if module_needs_fftw "$module"; then
        fftw_package=$(resolve_fftw_package)
        if [ -n "$fftw_package" ]; then
            packages="$packages $fftw_package"
        else
            libs="$libs -lfftw3f"
        fi
    fi

    if [ -n "$packages" ]; then
        $PKG_CONFIG --libs $packages
    fi
    if [ -n "$libs" ]; then
        printf '%s\n' "$libs"
    fi
}

build_model() {
    require_command "$PYTHON"
    mkdir -p "$(dirname "$MODEL")"
    "$PYTHON" scripts/create_identity_model.py "$MODEL"
}

build_program() {
    mkdir -p "$(dirname "$PROGRAM")"
    dep_cflags=$(dependency_cflags)
    dep_ldlibs=$(dependency_ldlibs)

    trace_on
    $CC $CPPFLAGS $CFLAGS $dep_cflags "$PROGRAM_SOURCE" \
        $LDFLAGS $dep_ldlibs $DEFAULT_LDLIBS \
        -o "$PROGRAM"
    trace_off
}

test_modules() {
    if [ -n "$TEST_MODULES" ]; then
        printf '%s\n' $TEST_MODULES
        return 0
    fi

    find src -iname "*.c" | grep -v '/main\.c$' | sort
}

run_tests() {
    test_modules | while read -r module; do
        name=$(basename "$module" | sed 's/\.c$//')
        test_exe="/tmp/${name}_test"

        if [ -n "$test_filter" ] \
           && [ "$test_filter" != "$name" ] \
           && [ "$test_filter" != "${name}.c" ]; then
            continue
        fi

        printf '\nTesting %s ...\n' "$module"

        flags=$(awk '/\/\/ flags:/ { $1=$2=""; print $0 }' "$module")
        dep_cflags=$(dependency_cflags_for_module "$module")
        dep_ldlibs=$(dependency_ldlibs_for_module "$module")

        trace_on
        if $CC $CPPFLAGS $CFLAGS $dep_cflags \
              "-DTESTING_$name=1" -DTESTING=1 "$module" \
              $LDFLAGS $dep_ldlibs $DEFAULT_LDLIBS $flags \
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

case "$command" in
build|all)
    build_program
    ;;
run)
    build_program
    trace_on
    "$PROGRAM" -i "$INPUT" -o "$OUTPUT" -m "$MODEL"
    trace_off
    ;;
test)
    run_tests
    ;;
debug)
    build_program
    ;;
check)
    build_program
    ;;
model)
    build_model
    ;;
setup)
    ./scripts/get_onnxruntime.sh
    build_model
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
