param(
  [ValidateSet('Debug','Release')][string]$Config = 'Release',
  [string]$VSGenerator = 'Visual Studio 17 2022'
)
set-strictmode -version latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Third    = Join-Path $RepoRoot 'third_party'
$Vcpkg    = Join-Path $Third 'vcpkg'
$OH264    = Join-Path $Third 'openh264'
$PjInstall= Join-Path $RepoRoot 'pjsip/pjproject/install'

# Quick checks
if (-not (Test-Path (Join-Path $Vcpkg 'installed/x64-windows/lib/SDL2.lib'))) {
  Write-Warning 'SDL2 not found in third_party/vcpkg. Running setup...'
  & (Join-Path $PSScriptRoot 'setup-windows.ps1')
}
if (-not (Test-Path (Join-Path $OH264 'build_x64/openh264.lib'))) {
  Write-Warning 'OpenH264 not built. Running setup...'
  & (Join-Path $PSScriptRoot 'setup-windows.ps1')
}
if (-not (Test-Path (Join-Path $PjInstall 'lib'))) {
  Write-Warning 'PJSIP install not found. Running setup...'
  & (Join-Path $PSScriptRoot 'setup-windows.ps1')
}

$BuildDir = Join-Path $RepoRoot 'build_x64'
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

cmake -S $RepoRoot -B $BuildDir -G "$VSGenerator" -A x64 `
  -DPJSIP_ROOT="$PjInstall" `
  -DOPENH264_DIR="$OH264" `
  -DVCPKG_DIR="$Vcpkg" | Write-Host

cmake --build $BuildDir --config $Config --parallel | Write-Host

Write-Host "\n[build] softphone built in: $BuildDir/$Config" -ForegroundColor Green
