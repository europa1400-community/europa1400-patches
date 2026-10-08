<#
.SYNOPSIS
    Prepares a development checkout: submodules, .env, toolchain check, optional private decompilation repository.
.DESCRIPTION
    - initializes vendor/minhook
    - creates .env from .env.example (GAME_DIR, DECOMP_DIR, GAME_EXE) if it does not exist
    - checks for Visual Studio (C++ x86 tools), uv and git
    - with -DecompUrl clones the private decompilation repository next to this one (needs access) and sets DECOMP_DIR
    Building and the CI need neither the game nor the decompilation; the replay tests and build tables do.
.EXAMPLE
    ./scripts/setup.ps1 -GameDir "C:\Games\Europa 1400 Gold"
    ./scripts/setup.ps1 -DecompUrl https://github.com/<org>/europa1400-decompilation.git
#>
param(
    [string]$GameDir,
    [string]$DecompUrl
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

git submodule update --init --recursive
if ($LASTEXITCODE) { throw "git submodule update failed" }

$envFile = Join-Path $root ".env"
if (-not (Test-Path $envFile)) { Copy-Item (Join-Path $root ".env.example") $envFile; Write-Output "created .env" }
function Set-EnvValue([string]$key, [string]$value) {
    $lines = Get-Content $envFile
    if ($lines -match "^$key=") { $lines = $lines -replace "^$key=.*$", "$key=$value" } else { $lines += "$key=$value" }
    Set-Content $envFile $lines
}
if ($GameDir) {
    if (-not (Test-Path (Join-Path $GameDir "game.ini"))) { throw "$GameDir has no game.ini" }
    Set-EnvValue "GAME_DIR" (Resolve-Path $GameDir).Path
}
if ($DecompUrl) {
    $decomp = Join-Path (Split-Path $root -Parent) "europa1400-decompilation"
    if (-not (Test-Path $decomp)) { git clone $DecompUrl $decomp; if ($LASTEXITCODE) { throw "clone failed (access to the private repository?)" } }
    Set-EnvValue "DECOMP_DIR" $decomp
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = if (Test-Path $vswhere) { & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath }
Write-Output ("Visual Studio C++ (x86): " + $(if ($vs) { $vs } else { "MISSING - install Visual Studio Build Tools with 'Desktop development with C++'" }))
Write-Output ("uv:                      " + $(if (Get-Command uv -ErrorAction SilentlyContinue) { "ok" } else { "MISSING - https://docs.astral.sh/uv/" }))
Get-Content $envFile | Where-Object { $_ -match "^(GAME_DIR|DECOMP_DIR|GAME_EXE)=" }
Write-Output "next: ./scripts/build.ps1 -Test"
