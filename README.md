# lrc_gen

C prototype for the lyric timestamp generation pipeline.

The repository now builds a single executable from `src/main.c`. Runtime mode is
selected by the input arguments:

- `--input-song PATH` extracts vocals from the original song.
- `--input-vocals PATH` skips extraction and uses an existing vocals file.
- `--input-lyrics PATH` enables LRC generation.
- Omitting `--input-lyrics` runs vocals-output mode only.

Passing both `--input-song` and `--input-vocals` is an error.

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

Build the executable:

```sh
./build.sh build
```

Run extraction only:

```sh
./build.sh run --input-song song.mp3 --output-vocals vocals.opus
```

Extract vocals and generate synced lyrics:

```sh
./build.sh run \
    --input-song song.mp3 \
    --input-lyrics full_lyrics.txt \
    --output-vocals vocals.opus \
    --output-lrc synced_lyrics.lrc
```

Generate synced lyrics from already extracted vocals:

```sh
./build.sh run \
    --input-vocals vocals.opus \
    --input-lyrics full_lyrics.txt \
    --output-lrc synced_lyrics.lrc
```

If `--output-lrc` is omitted, the executable derives it from the input path by
removing the final extension and appending `.lrc`. For example,
`song.mp3` becomes `song.lrc`. The derived output is rejected when that file
already exists.

The model arguments are optional. If omitted, the executable uses the default
relative paths compiled into `src/default_models.h`.

## Commands

```sh
./build.sh build         # build the executable
./build.sh run [args]    # build and run the executable
./build.sh test [module] # build and run embedded module tests
./build.sh check         # run GCC and Clang static analyzers
./build.sh clean         # remove generated build outputs
```

`./build.sh` without arguments is the same as `./build.sh build`.

## Default model paths

Canonical model-related command-line options are:

```text
--model-vocal
--model-ctc
--tokenizer
```

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
PKG_CONFIG_PATH=/opt/onnxruntime/lib/pkgconfig ./build.sh build
```
