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

## Prerequisites

- Linux on x86-64 or AArch64
- A C11 compiler
- GNU Make
- Python 3, only to regenerate the tiny ONNX model
- `curl` and `tar`, only for the setup helper

## Build and run

```sh
make setup
make
make run
```

`make setup` downloads the CPU-only ONNX Runtime 1.28.0 release into
`third_party/onnxruntime` and generates `models/identity.onnx` without external
Python packages.

Expected output:

```text
input : 1 -2.5 3.25 8
output: 1 -2.5 3.25 8
identity model verification passed
```

## Use an existing ONNX Runtime installation

Set `ONNXRUNTIME_ROOT` to a directory containing `include/onnxruntime_c_api.h`
and `lib/libonnxruntime.so`:

```sh
make clean
make ONNXRUNTIME_ROOT=/opt/onnxruntime
LD_LIBRARY_PATH=/opt/onnxruntime/lib ./build/ort-identity models/identity.onnx
```

## Regenerate the model

```sh
make model
```

The generator writes the ONNX protobuf directly, so it does not require the
`onnx` Python package.
