#!/bin/bash
# FFmpeg Fetch Script for Linux/macOS
# Downloads LGPL-licensed FFmpeg build to third_party/ffmpeg

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_DIR="${1:-$SCRIPT_DIR/../third_party/ffmpeg}"
TARGET_DIR="$(cd "$(dirname "$TARGET_DIR")" && pwd)/$(basename "$TARGET_DIR")"

echo "FFLUCE FFmpeg Fetch Script"
echo "=========================="
echo ""

# Detect platform
OS="$(uname -s)"
ARCH="$(uname -m)"

case "$OS" in
    Linux)
        # Use static builds from johnvansickle.com (LGPL)
        DOWNLOAD_URL="https://johnvansickle.com/ffmpeg/releases/ffmpeg-release-${ARCH}-static.tar.xz"
        ARCHIVE_EXT="tar.xz"
        ;;
    Darwin)
        # macOS - use Homebrew or evermeet builds
        echo "On macOS, we recommend installing FFmpeg via Homebrew:"
        echo "  brew install ffmpeg"
        echo ""
        echo "Or download from https://evermeet.cx/ffmpeg/"
        echo ""
        echo "Then set: export FFLUCE_FFMPEG_ROOT=/usr/local"
        exit 0
        ;;
    *)
        echo "Unsupported OS: $OS"
        exit 1
        ;;
esac

# Create target directory
mkdir -p "$TARGET_DIR"

ARCHIVE_PATH="$TARGET_DIR/ffmpeg.$ARCHIVE_EXT"

# Download
echo "Downloading FFmpeg (LGPL build)..."
echo "URL: $DOWNLOAD_URL"
curl -L "$DOWNLOAD_URL" -o "$ARCHIVE_PATH"

# Extract
echo "Extracting..."
cd "$TARGET_DIR"
tar -xf "$ARCHIVE_PATH"

# Organize - move from extracted dir to target
EXTRACTED_DIR=$(find . -maxdepth 1 -type d -name "ffmpeg-*" | head -1)
if [ -n "$EXTRACTED_DIR" ]; then
    echo "Organizing files..."
    mkdir -p bin
    mv "$EXTRACTED_DIR/ffmpeg" bin/ 2>/dev/null || true
    mv "$EXTRACTED_DIR/ffprobe" bin/ 2>/dev/null || true
    rm -rf "$EXTRACTED_DIR"
fi

# Clean up archive
rm -f "$ARCHIVE_PATH"

# Verify
if [ -x "$TARGET_DIR/bin/ffmpeg" ]; then
    echo ""
    echo "SUCCESS: FFmpeg installed to $TARGET_DIR"
    echo ""
    "$TARGET_DIR/bin/ffmpeg" -version | head -3
    echo ""
    echo "Set environment variable for CMake:"
    echo "  export FFLUCE_FFMPEG_ROOT=\"$TARGET_DIR\""
    echo ""
else
    echo "ERROR: FFmpeg executable not found after extraction"
    exit 1
fi
