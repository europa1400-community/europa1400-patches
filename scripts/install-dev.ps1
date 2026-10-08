<#
.SYNOPSIS
    Installs the staged build into the game (GAME_DIR from .env) the same way a release is installed, or removes it again.
.DESCRIPTION
    Copies build/stage/e1400patch, build/stage/patches and build/stage/mods into the game directory and runs
    e1400patch.exe install (game.ini [Network] Server= points to the loader; the previous entry is kept). With -Uninstall
    it runs e1400patch.exe uninstall and removes the copied folders. Existing settings (e1400patch.ini) are kept.
.EXAMPLE
    ./scripts/install-dev.ps1
    ./scripts/install-dev.ps1 -Uninstall
#>
param(
    [string]$BuildDir = "build",
    [switch]$Uninstall
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$envFile = Join-Path $root ".env"
if (Test-Path $envFile) {
    Get-Content $envFile | Where-Object { $_ -match "^\s*([A-Z_]+)\s*=\s*(.*)$" } | ForEach-Object {
        if (-not [Environment]::GetEnvironmentVariable($matches[1])) {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2].Trim('"'))
        }
    }
}
if (-not $env:GAME_DIR -or -not (Test-Path (Join-Path $env:GAME_DIR "game.ini"))) { throw "GAME_DIR (.env) must point to the game directory" }
$game = (Resolve-Path $env:GAME_DIR).Path
$tool = Join-Path $game "e1400patch\e1400patch.exe"

if ($Uninstall) {
    if (Test-Path $tool) { & $tool uninstall }
    foreach ($dir in "e1400patch", "patches", "mods") {
        $source = Join-Path $root "$BuildDir\stage\$dir"
        if (-not (Test-Path $source)) { continue }
        # remove only what the stage provides (other patches stay)
        Get-ChildItem $source | ForEach-Object {
            $target = Join-Path $game "$dir\$($_.Name)"
            if ($_.Name -ne "e1400patch.ini" -and (Test-Path $target)) { Remove-Item -Recurse -Force $target }
        }
    }
    Write-Output "removed from $game"
    exit 0
}

$stage = Join-Path $root "$BuildDir\stage"
if (-not (Test-Path (Join-Path $stage "e1400patch\server.dll"))) { throw "nothing staged in $stage; build first" }
foreach ($dir in "e1400patch", "patches", "mods") {
    $source = Join-Path $stage $dir
    if (Test-Path $source) {
        New-Item -ItemType Directory -Force (Join-Path $game $dir) | Out-Null
        Copy-Item -Recurse -Force (Join-Path $source "*") (Join-Path $game $dir)
    }
}
& $tool install
& $tool status
