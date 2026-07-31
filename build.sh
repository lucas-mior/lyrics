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

command=${1:-build}
test_filter=${2:-}

PYTHON=${PYTHON:-python3}
PKG_CONFIG=${PKG_CONFIG:-pkg-config}
ONNXRUNTIME_PKG_CONFIG_NAME=${ONNXRUNTIME_PKG_CONFIG_NAME:-onnxruntime}
FFTW_PKG_CONFIG_NAME=${FFTW_PKG_CONFIG_NAME:-fftw3f}
default_ffmpeg_packages="libavformat libavcodec libavutil libswresample"
FFMPEG_PKG_CONFIG_PACKAGES=${FFMPEG_PKG_CONFIG_PACKAGES:-}
if [ -z "$FFMPEG_PKG_CONFIG_PACKAGES" ]; then
    FFMPEG_PKG_CONFIG_PACKAGES=$default_ffmpeg_packages
fi

APP=${APP:-${2:-all}}
GET_VOICE_PROGRAM=${GET_VOICE_PROGRAM:-bin/get_voice}
GEN_LRC_RAW_PROGRAM=${GEN_LRC_RAW_PROGRAM:-bin/gen_lrc_raw}
GEN_LRC_PROGRAM=${GEN_LRC_PROGRAM:-bin/gen_lrc}
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
    xsel="xsel"
else
    xsel="cat"
fi

usage() {
    cat <<'USAGE'
usage: ./build.sh [command] [app-or-test]

commands:
    build    build all executables, or one selected app
    run      build and run one selected app
    test     build and run embedded module tests
    debug    build with debug flags and UBSan
    check    build with GCC static analyzer
    clean    remove generated build outputs
    help     show this message

environment:
    CC                           C compiler, default: cc or tcc for tests
    PYTHON                       Python interpreter, default: python3
    PKG_CONFIG                   pkg-config program, default: pkg-config
    ONNXRUNTIME_PKG_CONFIG_NAME  pkg-config package, default: onnxruntime
    FFTW_PKG_CONFIG_NAME         pkg-config package, default: fftw3f
    FFMPEG_PKG_CONFIG_PACKAGES   pkg-config packages for libav*
    APP                          app to build/run, default: all for build
    GET_VOICE_PROGRAM            get_voice executable, default: bin/get_voice
    GEN_LRC_RAW_PROGRAM          raw LRC executable, default: bin/gen_lrc_raw
    GEN_LRC_PROGRAM              full LRC executable, default: bin/gen_lrc
    INPUT                        run command input path, default: song.mp3
    OUTPUT                       get_voice run output path, default: vocals.wav
    CFLAGS                       extra compiler flags
    DEFAULT_LDLIBS               default libraries, default: -lm

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

resolve_ffmpeg_packages() {
    require_command $PKG_CONFIG

    for package in $FFMPEG_PKG_CONFIG_PACKAGES; do
        if ! $PKG_CONFIG --exists "$package"; then
            echo "pkg-config could not find $package" >&2
            echo "set FFMPEG_PKG_CONFIG_PACKAGES to the installed" >&2
            echo "libav packages" >&2
            exit 1
        fi
    done

    printf '%s\n' "$FFMPEG_PKG_CONFIG_PACKAGES"
}

resolve_include_path() {
    source_file="$1"
    include_file="$2"
    source_dir=$(dirname "$source_file")

    if [ -f "$source_dir/$include_file" ]; then
        printf '%s\n' "$source_dir/$include_file"
        return 0
    fi

    if [ -f "$include_file" ]; then
        printf '%s\n' "$include_file"
        return 0
    fi

    if [ -f "src/$include_file" ]; then
        printf '%s\n' "src/$include_file"
        return 0
    fi

    if [ -f "cbase/$include_file" ]; then
        printf '%s\n' "cbase/$include_file"
        return 0
    fi

    return 1
}

source_closure_add() {
    source_file="$1"

    case " $SOURCE_CLOSURE_SEEN " in
    *" $source_file "*)
        return 0
        ;;
    esac

    SOURCE_CLOSURE_SEEN="$SOURCE_CLOSURE_SEEN $source_file"
    printf '%s\n' "$source_file"

    includes=$(sed -n \
        's/^[[:space:]]*#[[:space:]]*include[[:space:]]*"\([^"]*\)".*/\1/p' \
        "$source_file")

    for include_file in $includes; do
        if include_path=$(resolve_include_path "$source_file" \
                "$include_file"); then
            source_closure_add "$include_path"
        fi
    done
}

source_closure() {
    SOURCE_CLOSURE_SEEN=""
    source_closure_add "$1"
}

source_closure_contains_file() {
    source_file="$1"
    pattern="$2"

    for dependency in $(source_closure "$source_file"); do
        case "$dependency" in
        $pattern)
            return 0
            ;;
        esac
    done

    return 1
}

source_closure_grep() {
    source_file="$1"
    pattern="$2"

    for dependency in $(source_closure "$source_file"); do
        if grep -Eq "$pattern" "$dependency"; then
            return 0
        fi
    done

    return 1
}

module_needs_onnxruntime() {
    source_closure_contains_file "$1" '*/ort.c' \
        || source_closure_grep "$1" 'onnxruntime_c_api.h|"ort\.c"'
}

module_needs_fftw() {
    source_closure_contains_file "$1" '*/fftw.c' \
        || source_closure_contains_file "$1" '*/stft.c'
}

module_needs_ffmpeg_libraries() {
    source_closure_grep "$1" \
        'libavformat/|libavcodec/|libavutil/|libswresample/'
}

package_list_add() {
    package_list="$1"
    package_name="$2"

    case " $package_list " in
    *" $package_name "*)
        printf '%s\n' "$package_list"
        return 0
        ;;
    esac

    printf '%s %s\n' "$package_list" "$package_name"
}

dependency_packages_for_module() {
    module="$1"
    packages=""

    if module_needs_onnxruntime "$module"; then
        packages=$(package_list_add "$packages" \
            "$(resolve_onnxruntime_package)")
    fi
    if module_needs_fftw "$module"; then
        fftw_package=$(resolve_fftw_package)
        if [ -n "$fftw_package" ]; then
            packages=$(package_list_add "$packages" "$fftw_package")
        fi
    fi
    if module_needs_ffmpeg_libraries "$module"; then
        for ffmpeg_package in $(resolve_ffmpeg_packages); do
            packages=$(package_list_add "$packages" "$ffmpeg_package")
        done
    fi

    printf '%s\n' "$packages"
}

dependency_cflags() {
    packages="$1"

    if [ -n "$packages" ]; then
        $PKG_CONFIG --cflags $packages
    fi
}

dependency_ldlibs() {
    packages="$1"

    if [ -n "$packages" ]; then
        $PKG_CONFIG --libs $packages
    fi
}

dependency_cflags_for_module() {
    dependency_cflags "$(dependency_packages_for_module "$1")"
}

dependency_ldlibs_for_module() {
    module="$1"
    packages=$(dependency_packages_for_module "$module")
    libs=""

    if module_needs_fftw "$module"; then
        fftw_package=$(resolve_fftw_package)
        if [ -z "$fftw_package" ]; then
            libs="$libs -lfftw3f"
        fi
    fi

    dependency_ldlibs "$packages"
    if [ -n "$libs" ]; then
        printf '%s\n' "$libs"
    fi
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
    dep_cflags=$(dependency_cflags_for_module "$source")
    dep_ldlibs=$(dependency_ldlibs_for_module "$source")
    app_cppflags=$(app_cppflags "$app")

    mkdir -p "$(dirname "$output")"

    trace_on
    $CC $CPPFLAGS $app_cppflags $CFLAGS $dep_cflags "$source" \
        $LDFLAGS $dep_ldlibs $DEFAULT_LDLIBS \
        -o "$output"
    trace_off
}

build_program() {
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

module_test_compile_command() {
    module="$1"
    name=$(module_test_name "$module")
    flags=$(module_test_flags "$module")
    dep_cflags=$(dependency_cflags_for_module "$module")
    dep_ldlibs=$(dependency_ldlibs_for_module "$module")
    test_exe="/tmp/${name}_test"

    printf '%s %s %s %s -DTESTING_%s=1 -DTESTING=1 %s ' \
        "$CC" "$CPPFLAGS" "$CFLAGS" "$dep_cflags" "$name" "$module"
    printf '%s %s %s %s -o %s\n' \
        "$LDFLAGS" "$dep_ldlibs" "$DEFAULT_LDLIBS" "$flags" "$test_exe"
}

build_deps_fail() {
    echo "build dependency test failed: $1" >&2
    exit 1
}

build_deps_assert_contains() {
    haystack="$1"
    needle="$2"
    message="$3"

    case "$haystack" in
    *"$needle"*)
        return 0
        ;;
    esac

    build_deps_fail "$message"
}

build_deps_assert_not_contains() {
    haystack="$1"
    needle="$2"
    message="$3"

    case "$haystack" in
    *"$needle"*)
        build_deps_fail "$message"
        ;;
    esac

    return 0
}

build_deps_make_fake_pkg_config() {
    fake_pkg_config="$1"

    cat >"$fake_pkg_config" <<'PKGCONFIG'
#!/bin/sh

package_exists() {
    case "$1" in
    onnxruntime|libonnxruntime|fftw3f|fftw3)
        return 0
        ;;
    libavformat|libavcodec|libavutil|libswresample)
        return 0
        ;;
    esac

    return 1
}

case "$1" in
--exists)
    shift
    for package in "$@"; do
        if ! package_exists "$package"; then
            exit 1
        fi
    done
    exit 0
    ;;
--cflags)
    shift
    for package in "$@"; do
        printf ' -I/pkg/%s/include' "$package"
    done
    printf '\n'
    ;;
--libs)
    shift
    for package in "$@"; do
        printf ' -l%s' "$package"
    done
    printf '\n'
    ;;
*)
    exit 2
    ;;
esac
PKGCONFIG
    chmod +x "$fake_pkg_config"
}

test_build_dependencies() {
    temp_dir="/tmp/lrc_gen_build_deps_$$"
    old_pkg_config=$PKG_CONFIG
    old_ort_package=$ONNXRUNTIME_PKG_CONFIG_NAME
    old_fftw_package=$FFTW_PKG_CONFIG_NAME
    old_ffmpeg_packages=$FFMPEG_PKG_CONFIG_PACKAGES

    rm -rf "$temp_dir"
    mkdir -p "$temp_dir"

    build_deps_make_fake_pkg_config "$temp_dir/pkg-config"
    PKG_CONFIG="$temp_dir/pkg-config"
    ONNXRUNTIME_PKG_CONFIG_NAME=onnxruntime
    FFTW_PKG_CONFIG_NAME=fftw3f
    FFMPEG_PKG_CONFIG_PACKAGES="libavformat libavcodec libavutil libswresample"

    cat >"$temp_dir/transitive.c" <<'SOURCE'
#include "src/main_get_voice.c"
SOURCE
    command_line=$(module_test_compile_command "$temp_dir/transitive.c")
    build_deps_assert_contains "$command_line" \
        "-I/pkg/onnxruntime/include" "missing transitive ONNX cflags"
    build_deps_assert_contains "$command_line" \
        "-lonnxruntime" "missing transitive ONNX libs"
    build_deps_assert_contains "$command_line" \
        "-I/pkg/fftw3f/include" "missing transitive FFTW cflags"
    build_deps_assert_contains "$command_line" \
        "-lfftw3f" "missing transitive FFTW libs"

    cat >"$temp_dir/ffmpeg_direct.c" <<'SOURCE'
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
SOURCE
    command_line=$(module_test_compile_command "$temp_dir/ffmpeg_direct.c")
    build_deps_assert_contains "$command_line" \
        "-I/pkg/libavformat/include" "missing FFmpeg cflags"
    build_deps_assert_contains "$command_line" \
        "-llibavformat" "missing FFmpeg libs"
    build_deps_assert_contains "$command_line" \
        "-llibswresample" "missing swresample libs"

    cat >"$temp_dir/plain.c" <<'SOURCE'
static int ignored;
SOURCE
    command_line=$(module_test_compile_command "$temp_dir/plain.c")
    build_deps_assert_not_contains "$command_line" \
        "onnxruntime" "plain source should not use ONNX"
    build_deps_assert_not_contains "$command_line" \
        "fftw3f" "plain source should not use FFTW"
    build_deps_assert_not_contains "$command_line" \
        "libavformat" "plain source should not use FFmpeg"

    PKG_CONFIG=$old_pkg_config
    ONNXRUNTIME_PKG_CONFIG_NAME=$old_ort_package
    FFTW_PKG_CONFIG_NAME=$old_fftw_package
    FFMPEG_PKG_CONFIG_PACKAGES=$old_ffmpeg_packages
    rm -rf "$temp_dir"

    printf 'Build dependency tests passed.\n'
}

run_tests() {
    if [ -z "$test_filter" ] \
       || [ "$test_filter" = build_deps ] \
       || [ "$test_filter" = build-deps ]; then
        test_build_dependencies
    fi

    if [ "$test_filter" = build_deps ] \
       || [ "$test_filter" = build-deps ]; then
        return 0
    fi

    find src -iname "*.c" | grep -v '/main[^/]*\.c$' | sort \
        | while read -r module; do
        name=$(module_test_name "$module")
        test_exe="/tmp/${name}_test"

        if [ -n "$test_filter" ] \
           && [ "$test_filter" != "$name" ] \
           && [ "$test_filter" != "${name}.c" ]; then
            continue
        fi

        printf '\nTesting %s ...\n' "$module"

        flags=$(module_test_flags "$module")
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
    build_program
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
