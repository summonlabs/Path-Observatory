<#
.SYNOPSIS
  Configure, build and test Path Observatory on Windows with the MSVC toolchain.

.DESCRIPTION
  The script imports the Visual Studio environment, then configures with Ninja
  or the Visual Studio generator, builds the requested configuration and
  optionally runs the test suite. It exists so that a reviewer can reproduce the
  release build with one command.

.EXAMPLE
  pwsh -File scripts/build.ps1 -Configuration Release -Test
#>
[CmdletBinding()]
param(
  [ValidateSet('Debug', 'Release')]
  [string] $Configuration = 'Release',
  [string] $BuildDirectory = '',
  [string] $Generator = 'Ninja',
  [switch] $Test,
  [switch] $Install,
  [switch] $Asan
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrEmpty($BuildDirectory)) {
  $BuildDirectory = Join-Path $root ("build/" + $Configuration.ToLowerInvariant())
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path $vswhere)) {
  throw "vswhere.exe was not found; a Visual Studio installation is required"
}
$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $installation) {
  throw "no Visual Studio installation with the C++ tools was found"
}
$vcvars = Join-Path $installation 'VC/Auxiliary/Build/vcvars64.bat'
if (-not (Test-Path $vcvars)) {
  throw "vcvars64.bat was not found under $installation"
}

Write-Host "Path Observatory build"
Write-Host "  source      : $root"
Write-Host "  build       : $BuildDirectory"
Write-Host "  generator   : $Generator"
Write-Host "  config      : $Configuration"

$configure = '"' + $vcvars + '" >nul 2>&1 && cmake -S "' + $root + '" -B "' + $BuildDirectory + '"'
if ($Generator -eq 'Ninja') {
  $configure += ' -G Ninja -DCMAKE_BUILD_TYPE=' + $Configuration
} else {
  $configure += ' -G "' + $Generator + '" -A x64'
}
$configure += ' -DPATHOBS_WARNINGS_AS_ERRORS=ON'
if ($Asan) { $configure += ' -DPATHOBS_ENABLE_ASAN=ON' }

cmd /c $configure
if ($LASTEXITCODE -ne 0) { throw "configure failed with exit code $LASTEXITCODE" }

$build = '"' + $vcvars + '" >nul 2>&1 && cmake --build "' + $BuildDirectory + '" --config ' + $Configuration + ' --parallel'
cmd /c $build
if ($LASTEXITCODE -ne 0) { throw "build failed with exit code $LASTEXITCODE" }

if ($Install) {
  $prefix = Join-Path $BuildDirectory 'install'
  cmd /c ('"' + $vcvars + '" >nul 2>&1 && cmake --install "' + $BuildDirectory + '" --config ' + $Configuration + ' --prefix "' + $prefix + '"')
  if ($LASTEXITCODE -ne 0) { throw "install failed with exit code $LASTEXITCODE" }
}

if ($Test) {
  Push-Location $BuildDirectory
  try {
    cmd /c ('"' + $vcvars + '" >nul 2>&1 && ctest --build-config ' + $Configuration + ' --output-on-failure')
    if ($LASTEXITCODE -ne 0) { throw "ctest failed with exit code $LASTEXITCODE" }
  } finally {
    Pop-Location
  }
}

Write-Host "done"
