# lrc_gen

C prototype for the lyric timestamp generation pipeline.

The repository is organized around three executable entry points:

1. `src/main_get_voice.c`
   - Takes an original music file and extracts the voice stem.
2. `src/main_gen_lrc_raw.c`
   - Takes an already extracted voice audio file plus plain-text lyrics and
     writes an `.lrc` file.
3. `src/main_gen_lrc.c`
   - Takes original music plus plain-text lyrics, extracts the voice, and then
     writes an `.lrc` file.

`src/main.c` is kept as a compatibility wrapper for `src/main_get_voice.c`.
New build commands use the explicit main files above.

## Reference working for separating vocals

```sh
pip install "audio-separator[gpu]" "audio-separator[cpu]"
audio-separator Paranoid.flac \
    --model_filename UVR-MDX-NET-Voc_FT.onnx \
    --single_stem Vocals \
    --output_format OPUS \
    --output_bitrate 96k
```

## Prerequisites

- Linux on x86-64 or AArch64
- A C11 compiler
- `pkg-config`
- FFmpeg at runtime for audio decoding/encoding

## Build and run

Build all configured executables:

```sh
./build.sh build
```

Build one executable:

```sh
./build.sh build get_voice
./build.sh build gen_lrc_raw
./build.sh build gen_lrc
```

Run the current voice extraction executable:

```sh
./build.sh run get_voice
```

The model arguments are optional. If omitted, the executables use the default
relative paths compiled into `src/default_models.h`.

## Commands

```sh
./build.sh build [app]    # build all executables, or one selected app
./build.sh run [app]      # build and run one selected app
./build.sh test [module]  # build and run embedded module tests
./build.sh check [app]    # run GCC and Clang static analyzers
./build.sh clean          # remove generated build outputs
```

Available app names:

```text
all
get_voice
gen_lrc_raw
gen_lrc
```

`./build.sh` without arguments is the same as `./build.sh build`.

## Default model paths

The command-line model options are optional. The compiled defaults are:

```text
models/UVR-MDX-NET-Voc_FT.onnx
models/mms-onnx/onnx/model.onnx
models/mms-onnx/tokens.txt
```

Command-line options still override these defaults. The existing environment
variables are still checked before falling back to the compiled defaults:

```text
LRC_VOCALS_MODEL
LRC_CTC_MODEL
LRC_CTC_TOKENIZER
```

## ONNX Runtime pkg-config

Install an ONNX Runtime package that provides `onnxruntime.pc`, or point
`PKG_CONFIG_PATH` at the directory containing it:

```sh
./build.sh clean
PKG_CONFIG_PATH=/opt/onnxruntime/lib/pkgconfig ./build.sh build get_voice
```
