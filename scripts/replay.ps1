<#
.SYNOPSIS
    Replays a recorded multiplayer session against the loader (server.dll shim + original + enabled patches) and compares
    every byte the server sends with the recording.
.DESCRIPTION
    Needs the game (GAME_DIR) and recordings. Recordings contain save games and IP addresses and are never committed; the
    default ones come from the decompilation repository (DECOMP_DIR\bin\recordings, masks in DECOMP_DIR\tests\server).
    Without -Patches the stage's patches are disabled: the loader must then be fully transparent (byte-identical).
.EXAMPLE
    ./scripts/replay.ps1                         # session01, loader without patches
    ./scripts/replay.ps1 -Patches                # with the patches of build/stage/patches enabled
    ./scripts/replay.ps1 -Recording D:\rec\x.e1rec -Mask D:\rec\x.noise.txt
    ./scripts/replay.ps1 -Original               # baseline: the original server.dll alone
#>
param(
    [string]$Recording,
    [string]$Mask,
    [string]$BuildDir = "build",
    [switch]$Patches,
    [switch]$Original
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

$envFile = Join-Path $root ".env"
if (Test-Path $envFile) {
    Get-Content $envFile | Where-Object { $_ -match "^\s*([A-Z_]+)\s*=\s*(.*)$" } | ForEach-Object {
        if (-not [Environment]::GetEnvironmentVariable($matches[1])) {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2].Trim('"'))
        }
    }
}
if (-not $env:GAME_DIR) { throw "GAME_DIR is not set (.env)" }
if (-not $Recording) {
    if (-not $env:DECOMP_DIR) { throw "no -Recording and no DECOMP_DIR (.env)" }
    $Recording = Join-Path $env:DECOMP_DIR "bin\recordings\session01-lan-2clients-15min.e1rec"
    if (-not $Mask) { $Mask = Join-Path $env:DECOMP_DIR "tests\server\session01.noise.txt" }
}
$originalDll = (Resolve-Path (Join-Path $env:GAME_DIR "Server\server.dll")).Path
$bin = Join-Path $root "$BuildDir\bin"
$stage = Join-Path $root "$BuildDir\stage"
$tested = if ($Original) { $originalDll } else { Join-Path $stage "e1400patch\server.dll" }

# the server reads game.ini next to the host executable (game data path, port)
Copy-Item (Join-Path $env:GAME_DIR "game.ini") (Join-Path $bin "game.ini") -Force
$gameIni = Join-Path $bin "game.ini"
(Get-Content $gameIni) -replace '^GamePath=.*$', "GamePath=$($env:GAME_DIR.TrimEnd('\'))\" | Set-Content $gameIni

# test configuration of the loader: patches off unless -Patches, log next to the build
$config = Join-Path $root "$BuildDir\replay-e1400patch.ini"
Set-Content $config "[loader]`r`n"
if (-not $Patches) {
    Get-ChildItem (Join-Path $stage "patches") -Directory -ErrorAction SilentlyContinue | ForEach-Object {
        Add-Content $config "[patches]`r`n$($_.Name)=0"
    }
}
$env:E1400PATCH_CONFIG = $config
$env:E1400PATCH_GAME_DIR = $env:GAME_DIR
$env:E1400PATCH_PATCHES_DIR = Join-Path $stage "patches"
$env:E1400PATCH_MODS_DIR = Join-Path $stage "mods"
$env:E1400PATCH_LOG = Join-Path $root "$BuildDir\replay-e1400patch.log"

$arguments = @((Resolve-Path $Recording).Path, $tested, "--original", $originalDll)
if ($Mask) { $arguments += @("--mask", (Resolve-Path $Mask).Path) }
& (Join-Path $bin "e1400replay.exe") @arguments | Select-String -Pattern "consumed|DIVERGENCE|PASS|FAIL|INCOMPLETE|CRASH|warning"
exit $LASTEXITCODE
