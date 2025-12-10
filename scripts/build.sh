#!/bin/bash
# FFLUCE Build Script for Linux/macOS

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

echo "FFLUCE Build Script"
echo "==================="
echo ""

# Check build directory exists
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "ERROR: Build not configured. Run scripts/configure.sh first."
    exit 1
fi

echo "Build Dir:  $BUILD_DIR"
echo "Build Type: $BUILD_TYPE"
echo "Jobs:       $JOBS"
echo ""

# Build
echo "Building..."
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$JOBS"

echo ""
echo "Build complete!"

# Try to find the built executable
EXECUTABLE=$(find "$BUILD_DIR" -type f -perm +111 -name "FFLUCE*" 2>/dev/null | head -1)
if [ -n "$EXECUTABLE" ]; then
    echo "Executable: $EXECUTABLE"
fi
