#!/bin/sh
set -eu

version=${ONNXRUNTIME_VERSION:-1.28.0}
architecture=$(uname -m)

case "$architecture" in
    x86_64)
        package="onnxruntime-linux-x64-$version"
        ;;
    aarch64|arm64)
        package="onnxruntime-linux-aarch64-$version"
        ;;
    *)
        echo "unsupported architecture: $architecture" >&2
        exit 1
        ;;
esac

archive="$package.tgz"
url="https://github.com/microsoft/onnxruntime/releases/download/v$version/$archive"

mkdir -p third_party

if [ ! -f "third_party/$archive" ]; then
    echo "downloading $url"
    curl --fail --location --output "third_party/$archive" "$url"
fi

rm -rf "third_party/$package"
tar -xzf "third_party/$archive" -C third_party

pcdir="third_party/$package/lib/pkgconfig"
mkdir -p "$pcdir"
cat > "$pcdir/onnxruntime.pc" <<EOF_PC
prefix=\${pcfiledir}/../..
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: ONNX Runtime
Description: ONNX Runtime C API
Version: $version
Libs: -L\${libdir} -lonnxruntime
Cflags: -I\${includedir}
EOF_PC
ln -sfn onnxruntime.pc "$pcdir/libonnxruntime.pc"
ln -sfn "$package" third_party/onnxruntime

echo "ONNX Runtime installed at third_party/onnxruntime"
echo "pkg-config file written to third_party/onnxruntime/lib/pkgconfig/onnxruntime.pc"
