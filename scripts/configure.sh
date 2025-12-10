#!/bin/bash
# FFLUCE Configure Script for Linux/macOS
# Runs CMake configuration

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJECT_ROOT/build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

echo "FFLUCE Configure Script"
echo "======================="
echo ""
echo "Project Root: $PROJECT_ROOT"
echo "Build Dir:    $BUILD_DIR"
echo "Build Type:   $BUILD_TYPE"
echo ""

# Check for CMake
if ! command -v cmake &> /dev/null; then
    echo "ERROR: CMake not found. Please install CMake."
    exit 1
fi

# Check for FFmpeg
FFMPEG_ROOT="${FFLUCE_FFMPEG_ROOT:-}"
if [ -z "$FFMPEG_ROOT" ]; then
    DEFAULT_FFMPEG="$PROJECT_ROOT/third_party/ffmpeg"
    if [ -x "$DEFAULT_FFMPEG/bin/ffmpeg" ]; then
        FFMPEG_ROOT="$DEFAULT_FFMPEG"
        echo "Using FFmpeg from: $FFMPEG_ROOT"
    else
        echo "WARNING: FFLUCE_FFMPEG_ROOT not set and FFmpeg not found in third_party/ffmpeg"
        echo "Run scripts/ffmpeg_fetch.sh first, or set FFLUCE_FFMPEG_ROOT"
    fi
fi

# Create build directory
mkdir -p "$BUILD_DIR"

# Configure
echo "Running CMake configure..."

CMAKE_ARGS=(
    -S "$PROJECT_ROOT"
    -B "$BUILD_DIR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
)

if [ -n "$FFMPEG_ROOT" ]; then
    CMAKE_ARGS+=(-DFFLUCE_FFMPEG_ROOT="$FFMPEG_ROOT")
fi

cmake "${CMAKE_ARGS[@]}"

echo ""
echo "Configuration complete!"
echo "Run scripts/build.sh to build the project."
