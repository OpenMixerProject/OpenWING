#!/usr/bin/env bash
# run-qt-demo-macos.sh - Build and run the Qt demo locally on macOS for testing

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build-macos"

echo "============================================="
echo "Terminating existing WING Qt Demo processes..."
echo "============================================="
pkill -x wing-qt-demo || killall wing-qt-demo || true

echo "============================================="
echo "Building WING Qt Demo on macOS..."
echo "============================================="

# Create build directory
mkdir -p "${BUILD_DIR}"

# Run CMake for the macOS host configuration
cmake -S "${ROOT_DIR}/software/qt-demo" -B "${BUILD_DIR}"

# Compile the Qt demo
echo ""
echo "Compiling..."
cmake --build "${BUILD_DIR}" -j"$(sysctl -n hw.ncpu)"

# Locate and run the executable
echo ""
echo "============================================="
echo "Launching Qt Demo..."
echo "============================================="

if [ -d "${BUILD_DIR}/wing-qt-demo.app" ]; then
    # If compiled as a macOS app bundle, run the binary inside
    "${BUILD_DIR}/wing-qt-demo.app/Contents/MacOS/wing-qt-demo"
elif [ -f "${BUILD_DIR}/wing-qt-demo" ]; then
    # If compiled as a standard Unix executable
    "${BUILD_DIR}/wing-qt-demo"
else
    echo "Error: Could not locate compiled wing-qt-demo executable in ${BUILD_DIR}"
    exit 1
fi
