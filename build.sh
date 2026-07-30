#!/bin/sh

if [ -n "$BASH_VERSION" ]; then
    # shellcheck disable=SC3044
    shopt -s expand_aliases
fi

alias trace_on='set -x'
alias trace_off='{ set +x; } 2>/dev/null'

set -eu

CC=${CC:-cc}
PYTHON=${PYTHON:-python3}
PKG_CONFIG=${PKG_CONFIG:-pkg-config}
ONNXRUNTIME_PKG_CONFIG_NAME=${ONNXRUNTIME_PKG_CONFIG_NAME:-onnxruntime}
FFTW_PKG_CONFIG_NAME=${FFTW_PKG_CONFIG_NAME:-fftw3f}

PROGRAM=${PROGRAM:-bin/uvr-c}
APP_SOURCES="src/app.c src/cli.c src/audio.c src/ort.c"
APP_SOURCES="$APP_SOURCES src/mdx.c src/stft.c src/fftw.c"
DEFAULT_SOURCES="src/main.c $APP_SOURCES"
SOURCES=${SOURCES:-$DEFAULT_SOURCES}
TEST_SOURCES=${TEST_SOURCES:-$APP_SOURCES}
MODEL=${MODEL:-models/identity.onnx}
INPUT=${INPUT:-song.mp3}
OUTPUT=${OUTPUT:-vocals.wav}

CFLAGS=${CFLAGS:-"-std=c11 -O2 -g -Wall -Wextra"}
EXTRA_CFLAGS=${EXTRA_CFLAGS:-}
EXTRA_LDFLAGS=${EXTRA_LDFLAGS:-}
EXTRA_LDLIBS=${EXTRA_LDLIBS:-}
DEFAULT_LDLIBS=${DEFAULT_LDLIBS:-"-lm"}

if [ -d third_party/onnxruntime/lib/pkgconfig ]; then
    ort_pc=third_party/onnxruntime/lib/pkgconfig
    PKG_CONFIG_PATH="$ort_pc${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    export PKG_CONFIG_PATH
fi

usage() {
    cat <<'USAGE'
usage: ./build.sh [command]

commands:
    build    build the executable (default)
    run      build and run the CLI parser
    test     build and run all src/*.c TESTING_ module tests
    model    regenerate models/identity.onnx
    setup    download ONNX Runtime and regenerate the identity model
    clean    remove generated build outputs
    help     show this message

environment:
    CC                           C compiler, default: cc
    PYTHON                       Python interpreter, default: python3
    PKG_CONFIG                   pkg-config program, default: pkg-config
    ONNXRUNTIME_PKG_CONFIG_NAME  pkg-config package, default: onnxruntime
    FFTW_PKG_CONFIG_NAME          pkg-config package, default: fftw3f
    PROGRAM                      output executable, default: bin/uvr-c
    SOURCES                      source files to compile
    TEST_SOURCES                 tested sources, default: src/*.c except main.c
    MODEL                        ONNX model path, default: models/identity.onnx
    INPUT                        run command input path, default: song.mp3
    OUTPUT                       run command output path, default: vocals.wav
    CFLAGS                       compiler flags
    EXTRA_CFLAGS                 additional compiler flags
    EXTRA_LDFLAGS                additional linker flags
    EXTRA_LDLIBS                 additional libraries
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
    require_command "$PKG_CONFIG"

    if "$PKG_CONFIG" --exists "$ONNXRUNTIME_PKG_CONFIG_NAME"; then
        printf '%s\n' "$ONNXRUNTIME_PKG_CONFIG_NAME"
        return 0
    fi

    if [ "$ONNXRUNTIME_PKG_CONFIG_NAME" = onnxruntime ] && \
            "$PKG_CONFIG" --exists libonnxruntime; then
        printf '%s\n' libonnxruntime
        return 0
    fi

    echo "pkg-config could not find $ONNXRUNTIME_PKG_CONFIG_NAME" >&2
    echo "run ./build.sh setup or set PKG_CONFIG_PATH" >&2
    echo "to the directory containing onnxruntime.pc" >&2
    exit 1
}

resolve_fftw_package() {
    require_command "$PKG_CONFIG"

    if "$PKG_CONFIG" --exists "$FFTW_PKG_CONFIG_NAME"; then
        printf '%s\n' "$FFTW_PKG_CONFIG_NAME"
        return 0
    fi

    if [ "$FFTW_PKG_CONFIG_NAME" = fftw3f ] && \
            "$PKG_CONFIG" --exists fftw3; then
        printf '%s\n' fftw3
        return 0
    fi

    printf '%s\n' ""
    return 0
}

dependency_cflags() {
    ort_package=$(resolve_onnxruntime_package)
    fftw_package=$(resolve_fftw_package)

    if [ -n "$fftw_package" ]; then
        "$PKG_CONFIG" --cflags "$ort_package" "$fftw_package"
        return 0
    fi

    "$PKG_CONFIG" --cflags "$ort_package"
}

dependency_ldlibs() {
    ort_package=$(resolve_onnxruntime_package)
    fftw_package=$(resolve_fftw_package)

    if [ -n "$fftw_package" ]; then
        "$PKG_CONFIG" --libs "$ort_package" "$fftw_package"
        return 0
    fi

    "$PKG_CONFIG" --libs "$ort_package"
    printf ' %s\n' '-lfftw3f'
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
    $CC $CFLAGS $EXTRA_CFLAGS $dep_cflags $SOURCES \
        $EXTRA_LDFLAGS $DEFAULT_LDLIBS $dep_ldlibs $EXTRA_LDLIBS \
        -o "$PROGRAM"
    trace_off
}

run_tests() {
    for source in $TEST_SOURCES; do
        module=$(basename "$source" .c)
        output="bin/test_$module"

        mkdir -p bin
        dep_cflags=$(dependency_cflags)
        dep_ldlibs=$(dependency_ldlibs)

        trace_on
        $CC $CFLAGS $EXTRA_CFLAGS $dep_cflags "-DTESTING_$module=1" \
            $TEST_SOURCES $EXTRA_LDFLAGS $DEFAULT_LDLIBS $dep_ldlibs \
            $EXTRA_LDLIBS -o "$output"
        bin/test_$module
        trace_off
    done
}

command=${1:-build}

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
