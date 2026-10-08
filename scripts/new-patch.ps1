<#
.SYNOPSIS
    Creates a new patch (or mod) from templates/patch: patches/<id>/ with manifest, source and CMakeLists.txt.
.EXAMPLE
    ./scripts/new-patch.ps1 netfix -Name "Network fixes"
    ./scripts/new-patch.ps1 mymod -Name "My mod" -Mod
#>
param(
    [Parameter(Mandatory)][ValidatePattern("^[a-z][a-z0-9_]*$")][string]$Id,
    [string]$Name = $Id,
    [switch]$Mod
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$dir = Join-Path $root "patches\$Id"
if (Test-Path $dir) { throw "$dir exists" }
New-Item -ItemType Directory $dir | Out-Null
$kind = if ($Mod) { "mod" } else { "patch" }
foreach ($file in Get-ChildItem (Join-Path $root "templates\patch") -File) {
    $text = (Get-Content $file.FullName -Raw).Replace("@ID@", $Id).Replace("@NAME@", $Name).Replace("@KIND@", $kind)
    $target = Join-Path $dir ($file.Name.Replace("patch_template", $Id))
    [IO.File]::WriteAllText($target, $text, (New-Object Text.UTF8Encoding $false))
}
if ($Mod) {
    $cmake = Join-Path $dir "CMakeLists.txt"
    (Get-Content $cmake -Raw).Replace("e1400_add_patch($Id ", "e1400_add_patch($Id MOD ") | Set-Content $cmake
}
Write-Output "created $dir - edit patch.ini (targets, builds, symbols) and $Id.c, then ./scripts/build.ps1"
