# FFLUCE Configure Script for Windows
# Runs CMake configuration

param(
    [string]$BuildType = "Release",
    [string]$Generator = "Visual Studio 17 2022",
    [string]$BuildDir = "$PSScriptRoot\..\build"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = [System.IO.Path]::GetFullPath("$PSScriptRoot\..")
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

Write-Host "FFLUCE Configure Script" -ForegroundColor Cyan
Write-Host "=======================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Project Root: $ProjectRoot"
Write-Host "Build Dir:    $BuildDir"
Write-Host "Build Type:   $BuildType"
Write-Host "Generator:    $Generator"
Write-Host ""

# Check for CMake
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Error "CMake not found. Please install CMake and add it to PATH."
    exit 1
}

# Check for FFmpeg
$FfmpegRoot = $env:FFLUCE_FFMPEG_ROOT
if (-not $FfmpegRoot) {
    $DefaultFfmpeg = "$ProjectRoot\third_party\ffmpeg"
    if (Test-Path "$DefaultFfmpeg\bin\ffmpeg.exe") {
        $FfmpegRoot = $DefaultFfmpeg
        Write-Host "Using FFmpeg from: $FfmpegRoot" -ForegroundColor Yellow
    } else {
        Write-Warning "FFLUCE_FFMPEG_ROOT not set and FFmpeg not found in third_party/ffmpeg"
        Write-Warning "Run scripts/ffmpeg_fetch.ps1 first, or set FFLUCE_FFMPEG_ROOT"
    }
}

# Create build directory
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
}

# Configure
Write-Host "Running CMake configure..." -ForegroundColor Green
$cmakeArgs = @(
    "-S", $ProjectRoot,
    "-B", $BuildDir,
    "-G", $Generator,
    "-DCMAKE_BUILD_TYPE=$BuildType"
)

if ($FfmpegRoot) {
    $cmakeArgs += "-DFFLUCE_FFMPEG_ROOT=$FfmpegRoot"
}

& cmake @cmakeArgs

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "Configuration complete!" -ForegroundColor Green
    Write-Host "Run scripts/build.ps1 to build the project."
} else {
    Write-Error "CMake configuration failed"
    exit 1
}
