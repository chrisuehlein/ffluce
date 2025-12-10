# FFmpeg Fetch Script for Windows
# Downloads LGPL-licensed FFmpeg build to third_party/ffmpeg

param(
    [string]$Version = "7.1",
    [string]$TargetDir = "$PSScriptRoot\..\third_party\ffmpeg"
)

$ErrorActionPreference = "Stop"

# FFmpeg LGPL builds from gyan.dev (Windows)
$BaseUrl = "https://www.gyan.dev/ffmpeg/builds"
$ArchiveName = "ffmpeg-release-essentials.zip"
$DownloadUrl = "$BaseUrl/$ArchiveName"

Write-Host "FFLUCE FFmpeg Fetch Script" -ForegroundColor Cyan
Write-Host "==========================" -ForegroundColor Cyan
Write-Host ""

# Create target directory
$TargetDir = [System.IO.Path]::GetFullPath($TargetDir)
if (-not (Test-Path $TargetDir)) {
    Write-Host "Creating directory: $TargetDir"
    New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null
}

$ZipPath = Join-Path $TargetDir $ArchiveName

# Download
Write-Host "Downloading FFmpeg (LGPL build)..."
Write-Host "URL: $DownloadUrl"
try {
    Invoke-WebRequest -Uri $DownloadUrl -OutFile $ZipPath -UseBasicParsing
} catch {
    Write-Error "Failed to download FFmpeg: $_"
    exit 1
}

# Extract
Write-Host "Extracting..."
try {
    Expand-Archive -Path $ZipPath -DestinationPath $TargetDir -Force
} catch {
    Write-Error "Failed to extract: $_"
    exit 1
}

# Find extracted folder and flatten structure
$ExtractedDir = Get-ChildItem -Path $TargetDir -Directory | Where-Object { $_.Name -like "ffmpeg-*" } | Select-Object -First 1
if ($ExtractedDir) {
    Write-Host "Organizing files..."

    # Move bin, include, lib to target root if they exist in subdirectory
    foreach ($subdir in @("bin", "include", "lib", "doc")) {
        $src = Join-Path $ExtractedDir.FullName $subdir
        $dst = Join-Path $TargetDir $subdir
        if (Test-Path $src) {
            if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
            Move-Item -Path $src -Destination $dst
        }
    }

    # Clean up extracted folder
    Remove-Item -Recurse -Force $ExtractedDir.FullName
}

# Clean up zip
Remove-Item -Force $ZipPath

# Verify
$FfmpegExe = Join-Path $TargetDir "bin\ffmpeg.exe"
if (Test-Path $FfmpegExe) {
    Write-Host ""
    Write-Host "SUCCESS: FFmpeg installed to $TargetDir" -ForegroundColor Green
    Write-Host ""

    # Show version
    & $FfmpegExe -version | Select-Object -First 3

    Write-Host ""
    Write-Host "Set environment variable for CMake:" -ForegroundColor Yellow
    Write-Host "  `$env:FFLUCE_FFMPEG_ROOT = `"$TargetDir`""
    Write-Host ""
} else {
    Write-Error "FFmpeg executable not found after extraction"
    exit 1
}
