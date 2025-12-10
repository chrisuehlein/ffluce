# FFLUCE

Audio/video assembler with FFmpeg and NVENC hardware encoding support.

## Features

- **Video assembly**: Loop clips, intros, overlays with crossfade transitions
- **Audio rendering**: Binaural beats, noise generation, file mixing
- **Hardware acceleration**: NVIDIA NVENC with automatic CPU fallback
- **Live streaming**: YouTube, Twitch, custom RTMP endpoints
- **Session logging**: Detailed render and stream diagnostics

## Requirements

- **CMake** 3.20+
- **C++20** compiler (MSVC 2022, GCC 11+, Clang 14+)
- **JUCE** 8.x (fetched automatically by CMake)
- **FFmpeg** (LGPL build recommended)

## Quick Start

### 1. Clone the repository

```bash
git clone https://github.com/chrisuehlein/ffluce.git
cd ffluce
```

### 2. Get FFmpeg

**Windows:**
```powershell
.\scripts\ffmpeg_fetch.ps1
```

**Linux/macOS:**
```bash
chmod +x scripts/*.sh
./scripts/ffmpeg_fetch.sh
```

Or install via package manager and set `FFLUCE_FFMPEG_ROOT`:
```bash
# macOS
brew install ffmpeg

# Ubuntu/Debian
sudo apt install ffmpeg

# Then set environment variable
export FFLUCE_FFMPEG_ROOT=/usr/local  # or wherever ffmpeg is installed
```

### 3. Configure

**Windows:**
```powershell
.\scripts\configure.ps1
```

**Linux/macOS:**
```bash
./scripts/configure.sh
```

The first configure will download JUCE automatically via CMake FetchContent (this may take a few minutes).

### 4. Build

**Windows:**
```powershell
.\scripts\build.ps1
```

**Linux/macOS:**
```bash
./scripts/build.sh
```

## Project Structure

```
ffluce/
├── src/              # Application source code
│   ├── core/         # Main app, window, process management
│   ├── audio/        # Audio sources (binaural, noise, file player)
│   ├── rendering/    # FFmpeg integration, timeline assembly
│   ├── streaming/    # Live streaming support
│   └── ui/           # User interface components
├── build/            # Build output (gitignored, created by CMake)
├── assets/           # Redistributable demo media (tracked)
├── library/          # User media files (gitignored)
├── scripts/          # Build helper scripts
├── licenses/         # Third-party license texts
└── docs/             # Documentation
```

## Media Files

- **assets/**: Small demo clips and icons (tracked in git)
- **library/**: Your personal video clips (gitignored, create locally)

## Configuration

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `FFLUCE_ENABLE_NVENC` | ON | Enable NVIDIA hardware encoding |
| `FFLUCE_FFMPEG_ROOT` | - | Path to FFmpeg installation |

### Environment Variables

- `FFLUCE_FFMPEG_ROOT`: Path to FFmpeg (with `bin/`, `lib/`, `include/` subdirs)

## Troubleshooting

### FFmpeg not found
Run the fetch script or set `FFLUCE_FFMPEG_ROOT` to your FFmpeg installation.

### NVENC not available
NVENC requires an NVIDIA GPU with encoding support. The app falls back to CPU encoding automatically.

### Build fails on first run
CMake FetchContent downloads JUCE on first configure (~100MB). This may take a few minutes. If it times out, delete `build/` and re-run configure.

## License

This project is licensed under **AGPL-3.0-or-later**. See [LICENSE](LICENSE).

### Third-Party Components

| Component | License | Notes |
|-----------|---------|-------|
| JUCE | AGPL-3.0 | Used under AGPL option |
| FFmpeg | LGPL-2.1 | LGPL build (no GPL/nonfree) |

See [NOTICE.md](NOTICE.md) and [licenses/](licenses/) for details.

## Contributing

Contributions welcome. Please ensure your code compiles and follows the existing style.
