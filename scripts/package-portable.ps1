param(
  [string]$Config = 'Release',
  [string]$Arch = 'x64',
  [string]$OutDir = ''
)
set-strictmode -version latest
$ErrorActionPreference = 'Stop'

function Invoke-Step {
  param([string]$Name, [scriptblock]$Action)
  Write-Host "[package] $Name" -ForegroundColor Cyan
  & $Action
}

$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutDir -or $OutDir.Trim() -eq '') {
  $OutDir = Join-Path $RepoRoot "dist/softphone_portable_${Arch}_${Config}"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$BuildDir = Join-Path $RepoRoot "build_${Arch}"
$BinDir = Join-Path $BuildDir $Config
$Exe = Join-Path $BinDir 'softphone.exe'
if (-not (Test-Path $Exe)) {
  throw "softphone.exe not found: $Exe (build first via scripts/build.ps1)"
}

# 1) Copy exe + local DLLs in build dir (SDL2/openh264 should already be here from CMake post-build)
Invoke-Step 'Stage exe and local DLLs' {
  Copy-Item $Exe $OutDir -Force
  Get-ChildItem -Path $BinDir -Filter '*.dll' -ErrorAction SilentlyContinue | ForEach-Object {
    Copy-Item $_.FullName $OutDir -Force
  }
}

# 2) App-local VC++ runtime (to run without installing VC Redist)
Invoke-Step 'Stage MSVC runtime (app-local)' {
  $vswhere = Join-Path ${Env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
  $rt = $null
  if (Test-Path $vswhere) {
    $inst = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Runtime -property installationPath | Select-Object -First 1
    if ($inst) {
      $rtCandidates = Get-ChildItem -Directory -Recurse -Path (Join-Path $inst 'VC/Redist/MSVC') -Filter 'Microsoft.VC14*.CRT' -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match '\\x64\\' }
      $rt = $rtCandidates | Sort-Object FullName -Descending | Select-Object -First 1
    }
  }
  if ($rt) {
    Copy-Item (Join-Path $rt.FullName '*.dll') $OutDir -Force -ErrorAction SilentlyContinue
  } else {
    Write-Host '[package] MSVC runtime not found via vswhere; skipping app-local CRT' -ForegroundColor Yellow
  }
}

# 3) Add a convenience launcher (edit credentials as needed)
Invoke-Step 'Create run-softphone.cmd' {
  $cmd = @(
    '@echo off',
    'setlocal',
    'set EXE=%~dp0softphone.exe',
    'if not exist "%EXE%" ( echo softphone.exe not found & exit /b 1 )',
    'echo Running: softphone.exe 1073 pass1073 192.168.100.200',
    '"%EXE%" 1073 pass1073 192.168.100.200',
    'endlocal'
  ) -join "`r`n"
  Set-Content -Path (Join-Path $OutDir 'run-softphone.cmd') -Value $cmd -Encoding ASCII
}

# 4) README
Invoke-Step 'Write README.txt' {
  $readme = @(
    'TonklaSoftphone Portable',
    '=========================',
    '',
    'Contents:',
    ' - softphone.exe',
    ' - Required DLLs (SDL2, OpenH264, MSVC runtime if available)',
    '',
    'Usage:',
    ' - Edit run-softphone.cmd if you need different credentials/host',
    ' - Double-click run-softphone.cmd or run from terminal:',
    '     softphone.exe <extension> <password> <server>',
    '',
    'Notes:',
    ' - All DLLs are placed side-by-side with the exe for portability.',
    ' - If launching fails due to missing MSVC runtime, install VC++ 2022 x64 Redistributable or copy CRT DLLs app-locally.'
  ) -join "`r`n"
  Set-Content -Path (Join-Path $OutDir 'README.txt') -Value $readme -Encoding UTF8
}

# 5) Zip
Invoke-Step 'Create ZIP artifact' {
  $ZipPath = Join-Path $RepoRoot ("dist/softphone_portable_${Arch}_${Config}.zip")
  if (Test-Path $ZipPath) { Remove-Item $ZipPath -Force }
  Compress-Archive -Path (Join-Path $OutDir '*') -DestinationPath $ZipPath -Force
  Write-Host "[package] Portable ZIP: $ZipPath" -ForegroundColor Green
}

Write-Host "`n[package] Done: $OutDir" -ForegroundColor Green

