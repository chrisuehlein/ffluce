# FFLUCE Build Script for Windows

param(
    [string]$BuildType = "Release",
    [string]$BuildDir = "$PSScriptRoot\..\build",
    [int]$Jobs = 0
)

$ErrorActionPreference = "Stop"
$BuildDir = [System.IO.Path]::GetFullPath($BuildDir)

Write-Host "FFLUCE Build Script" -ForegroundColor Cyan
Write-Host "===================" -ForegroundColor Cyan
Write-Host ""

# Check build directory exists
if (-not (Test-Path "$BuildDir\CMakeCache.txt")) {
    Write-Error "Build not configured. Run scripts/configure.ps1 first."
    exit 1
}

# Determine job count
if ($Jobs -eq 0) {
    $Jobs = [Environment]::ProcessorCount
}

Write-Host "Build Dir:  $BuildDir"
Write-Host "Build Type: $BuildType"
Write-Host "Jobs:       $Jobs"
Write-Host ""

# Build
Write-Host "Building..." -ForegroundColor Green
cmake --build $BuildDir --config $BuildType --parallel $Jobs

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "Build complete!" -ForegroundColor Green

    # Try to find the built executable
    $ExePath = Get-ChildItem -Path $BuildDir -Recurse -Filter "*.exe" |
               Where-Object { $_.Name -notmatch "cmake" } |
               Select-Object -First 1

    if ($ExePath) {
        Write-Host "Executable: $($ExePath.FullName)"
    }
} else {
    Write-Error "Build failed"
    exit 1
}
