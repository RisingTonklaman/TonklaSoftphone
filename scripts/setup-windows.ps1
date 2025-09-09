param(
  [string]$VSGenerator = 'Visual Studio 17 2022',
  [switch]$BuildPjsip = $false
)
set-strictmode -version latest
$ErrorActionPreference = 'Stop'

function Invoke-Step {
  param([string]$Name, [scriptblock]$Action)
  Write-Host "[setup] $Name" -ForegroundColor Cyan
  & $Action
}

function Assert-Cmd {
  param([string]$Cmd)
  if (-not (Get-Command $Cmd -ErrorAction SilentlyContinue)) {
    throw "Required command not found: $Cmd"
  }
}

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Third = Join-Path $RepoRoot 'third_party'
New-Item -ItemType Directory -Force -Path $Third | Out-Null

# 1) Ensure cmake/git present
Invoke-Step 'Check prerequisites (git, cmake)' {
  Assert-Cmd git
  Assert-Cmd cmake
}

# 2) vcpkg + SDL2
$VcpkgRoot = Join-Path $Third 'vcpkg'
Invoke-Step 'Bootstrap vcpkg and install SDL2:x64-windows' {
  if (-not (Test-Path $VcpkgRoot)) {
    git clone https://github.com/microsoft/vcpkg $VcpkgRoot | Out-Null
  }
  & (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat') | Write-Host
  & (Join-Path $VcpkgRoot 'vcpkg.exe') install sdl2:x64-windows --clean-after-build | Write-Host
}

# 3) OpenH264 build (shared lib)
$OpenH264Root = Join-Path $Third 'openh264'
$OpenH264Build = Join-Path $OpenH264Root 'build_x64'

function Stage-OpenH264FromVcpkg {
  Write-Host "[setup] Fallback: install openh264 via vcpkg" -ForegroundColor Yellow
  & (Join-Path $VcpkgRoot 'vcpkg.exe') install openh264:x64-windows --clean-after-build | Write-Host
  $pkgBin = Join-Path $VcpkgRoot 'installed/x64-windows/bin'
  $pkgLib = Join-Path $VcpkgRoot 'installed/x64-windows/lib'
  $dll = Get-ChildItem -Path $pkgBin -Filter 'openh264*.dll' -ErrorAction SilentlyContinue | Select-Object -First 1
  $lib = Get-ChildItem -Path $pkgLib -Filter 'openh264*.lib' -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $dll -or -not $lib) { throw 'vcpkg openh264 not found after install' }
  New-Item -ItemType Directory -Force -Path $OpenH264Build | Out-Null
  # Preserve original DLL name and also provide compatibility aliases (-7 and -8)
  $origName = Split-Path $dll.Name -Leaf
  Copy-Item $dll.FullName (Join-Path $OpenH264Build $origName) -Force
  Copy-Item $lib.FullName (Join-Path $OpenH264Build 'openh264.lib') -Force
  # Create aliases if missing
  $alias7 = Join-Path $OpenH264Build 'openh264-7.dll'
  $alias8 = Join-Path $OpenH264Build 'openh264-8.dll'
  if (-not (Test-Path $alias7)) { Copy-Item (Join-Path $OpenH264Build $origName) $alias7 -Force }
  if (-not (Test-Path $alias8)) { Copy-Item (Join-Path $OpenH264Build $origName) $alias8 -Force }
  Write-Host "[setup] Staged OpenH264 from vcpkg:" -ForegroundColor DarkCyan
  Write-Host "  DLLs: $($dll.FullName) -> $OpenH264Build\$origName, openh264-7.dll, openh264-8.dll"
  Write-Host "  LIB : $($lib.FullName) -> $OpenH264Build\openh264.lib"
}

Invoke-Step 'Fetch and build OpenH264 (x64 Release, shared)' {
  if (-not (Test-Path $OpenH264Root)) {
    git clone --depth 1 --branch v2.4.1 https://github.com/cisco/openh264 $OpenH264Root | Out-Null
  }
  # If directory exists but not a git repo and no CMakeLists, reclone
  if (-not (Test-Path (Join-Path $OpenH264Root 'CMakeLists.txt'))) {
    Write-Host "[setup] openh264 dir exists but invalid; recloning..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $OpenH264Root -ErrorAction SilentlyContinue
    git clone --depth 1 --branch v2.4.1 https://github.com/cisco/openh264 $OpenH264Root | Out-Null
  }

  if (Test-Path (Join-Path $OpenH264Root 'CMakeLists.txt')) {
    New-Item -ItemType Directory -Force -Path $OpenH264Build | Out-Null
    try {
      cmake -S $OpenH264Root -B $OpenH264Build -G "$VSGenerator" -A x64 -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Release | Write-Host
      cmake --build $OpenH264Build --config Release --parallel | Write-Host
    } catch {
      Write-Host "[setup] CMake build of openh264 failed: $($_.Exception.Message)" -ForegroundColor Yellow
    }
  }

  # Try to collect artifacts from build tree
  $dllCandidates = @(Get-ChildItem -Recurse $OpenH264Build -Filter 'openh264*.dll' -ErrorAction SilentlyContinue)
  $libCandidates = @(Get-ChildItem -Recurse $OpenH264Build -Filter 'openh264*.lib' -ErrorAction SilentlyContinue)

  if (-not $dllCandidates -or $dllCandidates.Count -eq 0 -or -not $libCandidates -or $libCandidates.Count -eq 0) {
    Stage-OpenH264FromVcpkg
  } else {
    $dll = $dllCandidates | Where-Object { $_.FullName -match '\\Release\\' } | Select-Object -First 1
    if (-not $dll) { $dll = $dllCandidates | Select-Object -First 1 }
    $lib = $libCandidates | Where-Object { $_.FullName -match '\\Release\\' } | Select-Object -First 1
    if (-not $lib) { $lib = $libCandidates | Select-Object -First 1 }
    # Preserve original DLL name and also provide compatibility aliases (-7 and -8)
    $origName = Split-Path $dll.Name -Leaf
    Copy-Item $dll.FullName (Join-Path $OpenH264Build $origName) -Force
    Copy-Item $lib.FullName (Join-Path $OpenH264Build 'openh264.lib') -Force
    $alias7 = Join-Path $OpenH264Build 'openh264-7.dll'
    $alias8 = Join-Path $OpenH264Build 'openh264-8.dll'
    if (-not (Test-Path $alias7)) { Copy-Item (Join-Path $OpenH264Build $origName) $alias7 -Force }
    if (-not (Test-Path $alias8)) { Copy-Item (Join-Path $OpenH264Build $origName) $alias8 -Force }
    Write-Host "[setup] Staged OpenH264 from local build:" -ForegroundColor DarkCyan
    Write-Host "  DLLs: $($dll.FullName) -> $OpenH264Build\$origName, openh264-7.dll, openh264-8.dll"
    Write-Host "  LIB : $($lib.FullName) -> $OpenH264Build\openh264.lib"
  }
}

# 4) Build PJSIP from in-tree pjproject (optional)
if ($BuildPjsip) {
  $PjRoot = Join-Path $RepoRoot 'pjsip/pjproject'
  if (-not (Test-Path $PjRoot)) {
    Invoke-Step 'Clone pjproject (PJSIP sources)' {
      git clone https://github.com/pjsip/pjproject $PjRoot | Out-Null
    }
  }
  $Sln = Join-Path $PjRoot 'pjproject-vs14.sln'
  if (-not (Test-Path $Sln)) { throw "PJSIP solution not found: $Sln" }

  # Find MSBuild
  $vswhere = Join-Path ${Env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
  if (-not (Test-Path $vswhere)) { $vswhere = $null }
  $msbuild = $null
  if ($vswhere) {
    $msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
  }
  if (-not $msbuild) { $msbuild = (Get-Command msbuild.exe -ErrorAction SilentlyContinue).Source }
  if (-not $msbuild) { throw 'MSBuild not found. Install Visual Studio Build Tools with C++ workload.' }

  # Ensure config_site.h enables video + SDL + OpenH264
  $CfgDst = Join-Path $PjRoot 'pjlib/include/pj/config_site.h'
  if (-not (Test-Path $CfgDst)) {
    $CfgSrc = Join-Path $RepoRoot 'scripts/pjsip-config/config_site.h'
    if (Test-Path $CfgSrc) { Copy-Item $CfgSrc $CfgDst -Force }
  }

  Invoke-Step 'Build PJSIP (Release|x64)' {
    & $msbuild $Sln /p:Configuration=Release /p:Platform=x64 /m | Write-Host
  }

  Invoke-Step 'Package PJSIP headers and libs into vendor/pjsip/install/' {
    $InstallInc = Join-Path $RepoRoot 'vendor/pjsip/install/include'
    $InstallLib = Join-Path $RepoRoot 'vendor/pjsip/install/lib'
    New-Item -ItemType Directory -Force -Path $InstallInc,$InstallLib | Out-Null

    # Copy headers from each component include/
    $incDirs = @(
      'pjlib/include','pjlib-util/include','pjmedia/include','pjnath/include','pjsip/include'
    )
    foreach ($d in $incDirs) {
      $src = Join-Path $PjRoot $d
      if (Test-Path $src) {
        robocopy $src $InstallInc /E /NFL /NDL /NJH /NJS /NP | Out-Null
      }
    }

    # Copy aggregated lib
    $lib = Get-ChildItem -Path (Join-Path $PjRoot 'lib') -Filter 'libpjproject-*.lib' -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $lib) {
      throw 'libpjproject-*.lib not found under pjproject/lib. Build may have failed.'
    }
    Copy-Item $lib.FullName (Join-Path $InstallLib $lib.Name) -Force
  }
}

Write-Host "\n[setup] Done. Next: scripts/build.ps1" -ForegroundColor Green
