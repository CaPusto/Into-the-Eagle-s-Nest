#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

echo "Building for Linux..."
cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release "${SCRIPT_DIR}"
cmake --build "${BUILD_DIR}" --config Release -j$(nproc)

echo "Build complete: ${BUILD_DIR}/eaglenest"