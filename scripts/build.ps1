<#
.SYNOPSIS
    Configure, build and test with MSVC (x86) from any shell.
.EXAMPLE
    ./scripts/build.ps1                  # RelWithDebInfo into build/
    ./scripts/build.ps1 -Config Debug -Test
#>
param(
    [ValidateSet("Debug", "RelWithDebInfo", "Release")][string]$Config = "RelWithDebInfo",
    [string]$BuildDir = "build",
    [switch]$Test,
    [switch]$Clean
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

# x86 developer environment (vcvarsall via vswhere), unless the shell already has one
if (-not $env:VSCMD_ARG_TGT_ARCH -or $env:VSCMD_ARG_TGT_ARCH -ne "x86") {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "Visual Studio (Build Tools) with C++ workload not found (vswhere missing)" }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw "No Visual Studio installation with the x86/x64 C++ tools found" }
    $vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvarsall.bat"
    $hostArch = if ([Environment]::Is64BitOperatingSystem) { "x64_x86" } else { "x86" }
    cmd /c "`"$vcvars`" $hostArch >nul && set" | ForEach-Object {
        if ($_ -match "^([^=]+)=(.*)$") { [Environment]::SetEnvironmentVariable($matches[1], $matches[2]) }
    }
    # CMake and Ninja shipped with Visual Studio, if not on PATH
    $cmakeDir = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake"
    if (Test-Path $cmakeDir) { $env:PATH = "$cmakeDir\CMake\bin;$cmakeDir\Ninja;$env:PATH" }
}

if ($Clean -and (Test-Path $BuildDir)) { Remove-Item -Recurse -Force $BuildDir }
cmake -S . -B $BuildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config"
if ($LASTEXITCODE) { exit $LASTEXITCODE }
cmake --build $BuildDir
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if ($Test) {
    ctest --test-dir $BuildDir --output-on-failure
    exit $LASTEXITCODE
}
