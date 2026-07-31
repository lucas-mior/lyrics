# lrc_gen

C prototype for the lyric timestamp generation pipeline.

The repository is organized around three executable entry points:

1. `src/main_get_voice.c`
   - Takes an original music file and extracts the voice stem.
   - This is what the previous `src/main.c` executable did.
2. `src/main_gen_lrc_raw.c`
   - Takes an already extracted voice audio file plus plain-text lyrics and
     writes an `.lrc` file.
   - This is a dummy executable for now.
3. `src/main_gen_lrc.c`
   - Takes original music plus plain-text lyrics, extracts the voice, and then
     writes an `.lrc` file.
   - This is a dummy executable for now.

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

The raw and full LRC executables are intentionally dummy programs right now.
They compile, print their expected command-line shape, and report that the real
functionality is not implemented yet.

## Commands

```sh
./build.sh build [app]    # build all executables, or one selected app
./build.sh run [app]      # build and run one selected app
./build.sh test [module]  # build and run embedded module tests
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

## ONNX Runtime pkg-config

Install an ONNX Runtime package that provides `onnxruntime.pc`, or point
`PKG_CONFIG_PATH` at the directory containing it:

```sh
./build.sh clean
PKG_CONFIG_PATH=/opt/onnxruntime/lib/pkgconfig ./build.sh build get_voice
```
