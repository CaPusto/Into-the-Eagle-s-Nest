#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

echo "Building for macOS..."
cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release "${SCRIPT_DIR}"
cmake --build "${BUILD_DIR}" --config Release -j$(sysctl -n hw.ncpu)

echo "Build complete: ${BUILD_DIR}/eaglenest"

if [[ "$1" == "--universal" ]]; then
    echo "Creating universal binary..."
    cmake -B "${BUILD_DIR}_arm64" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 "${SCRIPT_DIR}"
    cmake --build "${BUILD_DIR}_arm64" --config Release

    cmake -B "${BUILD_DIR}_x86_64" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64 "${SCRIPT_DIR}"
    cmake --build "${BUILD_DIR}_x86_64" --config Release

    lipo -create \
        "${BUILD_DIR}/eaglenest" \
        "${BUILD_DIR}_arm64/eaglenest" \
        "${BUILD_DIR}_x86_64/eaglenest" \
        -output "${BUILD_DIR}/eaglenest_universal"
    echo "Universal binary: ${BUILD_DIR}/eaglenest_universal"
fi