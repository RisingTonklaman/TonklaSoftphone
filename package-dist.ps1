Param(
    [switch]$SkipBuild
)

# Build + package script for TonklaSoftphone
# Produces a dist folder at <project>/dist and writes softphone.zip there (overwrites existing).

$ErrorActionPreference = 'Stop'

$root = (Get-Item -Path $PSScriptRoot).Parent.FullName
$buildDir = Join-Path $root 'build_x64'
$dist = Join-Path $root 'dist'
$cfg = 'Release'

Write-Host "Project root: $root"

try {
    if (-not $SkipBuild) {
        if (Test-Path $buildDir) {
            Write-Host "Removing existing build folder: $buildDir"
            Remove-Item $buildDir -Recurse -Force
        }
        New-Item -ItemType Directory -Path $buildDir | Out-Null

        Push-Location $buildDir
        Write-Host "Configuring CMake (x64, $cfg)..."
        cmake -S $root -B $buildDir -A x64 -DCMAKE_BUILD_TYPE=$cfg

        Write-Host "Building (Release)..."
        cmake --build $buildDir --config $cfg -- -m
        Pop-Location
    } else {
        Write-Host "Skipping build (SkipBuild switch was provided)."
    }

    # Prepare dist folder
    if (Test-Path $dist) {
        Write-Host "Removing existing dist: $dist"
        Remove-Item $dist -Recurse -Force
    }
    New-Item -ItemType Directory -Path $dist | Out-Null

    # Find built exe (softphone.exe) and copy it
    $exe = Get-ChildItem -Path $buildDir -Filter softphone.exe -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($exe) {
        Write-Host "Copying executable: $($exe.FullName) -> $dist"
        Copy-Item $exe.FullName -Destination $dist -Force
    } else {
        Write-Host "Warning: softphone.exe not found in build output."
    }

    # Copy any DLLs found in Release folders under buildDir (e.g. SDL2.dll, openh264-*.dll)
    $releaseDirs = Get-ChildItem -Path $buildDir -Recurse -Directory -ErrorAction SilentlyContinue | Where-Object { $_.Name -ieq $cfg }
    if ($releaseDirs.Count -eq 0) { $releaseDirs = @((Get-Item -Path $buildDir)) }
    foreach ($rd in $releaseDirs) {
        Get-ChildItem -Path $rd.FullName -Filter *.dll -File -ErrorAction SilentlyContinue | ForEach-Object {
            Write-Host "Copying DLL: $($_.Name)"
            Copy-Item $_.FullName -Destination $dist -Force
        }
    }

    # Also copy any known runtime DLLs from top-level build/Release if present (project historically had build/Release)
    $legacyRel = Join-Path $root 'build\Release'
    if (Test-Path $legacyRel) {
        Get-ChildItem -Path $legacyRel -Filter *.dll -File -ErrorAction SilentlyContinue | ForEach-Object {
            Write-Host "Copying legacy Release DLL: $($_.Name)"
            Copy-Item $_.FullName -Destination $dist -Force
        }
    }

    # Copy README/LICENCE if present
    foreach ($fn in @('README.md','LICENSE','LICENSE.txt')) {
        $src = Join-Path $root $fn
        if (Test-Path $src) { Copy-Item $src -Destination $dist -Force }
    }

    # Create ZIP
    $zipPath = Join-Path $dist 'softphone.zip'
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Write-Host "Creating ZIP: $zipPath"
    Compress-Archive -Path (Join-Path $dist '*') -DestinationPath $zipPath -Force

    Write-Host "Packaging complete: $zipPath"
} catch {
    Write-Error "Packaging failed: $_"
    exit 1
}

exit 0
