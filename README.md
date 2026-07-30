# ONNX Runtime C identity example

A minimal Linux C application that exercises the ONNX Runtime C API without
introducing model-specific preprocessing or prediction logic.

The included model has one `float32[1,4]` input named `input`, one
`float32[1,4]` output named `output`, and a single `Identity` node. The program:

1. Initializes the ONNX Runtime API.
2. Creates an environment, session options, and session.
3. Creates a tensor backed by a C array.
4. Runs inference.
5. Checks output type, shape, element count, and values.
6. Releases every ONNX Runtime object.

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
- Python 3, only to regenerate the tiny ONNX model
- `curl` and `tar`, only for the setup helper

## Build and run

```sh
./build.sh setup
./build.sh run
```

`./build.sh setup` downloads the CPU-only ONNX Runtime 1.28.0 release into
`third_party/onnxruntime`, writes a local `onnxruntime.pc` file, and generates
`models/identity.onnx` without external Python packages. Later `build` and `run`
commands use `pkg-config` to obtain ONNX Runtime compiler and linker flags.

Expected output:

```text
input : 1 -2.5 3.25 8
output: 1 -2.5 3.25 8
identity model verification passed
```

## Commands

```sh
./build.sh build    # build the model and executable
./build.sh run      # build and run the example
./build.sh model    # regenerate models/identity.onnx
./build.sh setup    # download ONNX Runtime and regenerate the model
./build.sh clean    # remove generated build outputs
```

`./build.sh` without arguments is the same as `./build.sh build`.

## Use an existing ONNX Runtime installation

Install an ONNX Runtime package that provides a pkg-config file, or point
`PKG_CONFIG_PATH` at a directory containing `onnxruntime.pc`:

```sh
./build.sh clean
PKG_CONFIG_PATH=/opt/onnxruntime/lib/pkgconfig ./build.sh build
LD_LIBRARY_PATH=/opt/onnxruntime/lib ./bin/ort-identity models/identity.onnx
```

If your installation uses a different pkg-config package name, set
`ONNXRUNTIME_PKG_CONFIG_NAME`:

```sh
ONNXRUNTIME_PKG_CONFIG_NAME=libonnxruntime ./build.sh build
```

## Regenerate the model

```sh
./build.sh model
```

The generator writes the ONNX protobuf directly, so it does not require the
`onnx` Python package.
