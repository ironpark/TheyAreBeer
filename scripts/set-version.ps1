# Set the mod version: "Version" in mod\ModInfos.json, the one version number of the mod (scripts\build.ps1 compiles it
# into TABPerf.dll, scripts\package.ps1 names the package after it, TABModLoader's auto-update compares it with the tag
# of the latest GitHub release).
#   powershell -ExecutionPolicy Bypass -File scripts\set-version.ps1 -Version 0.0.2     (or v0.0.2)
# The release workflow (.github\workflows\build.yml) runs it with the pushed tag, so a release built from tag v<x>
# always carries version <x>.
param([Parameter(Mandatory = $true)][string]$Version)
$ErrorActionPreference = 'Stop'
$v = $Version.Trim().TrimStart('v', 'V')
if ($v -notmatch '^\d+(\.\d+){1,3}$') { throw "Version must look like 1.2.3 (or v1.2.3), got: $Version" }
$path = Join-Path (Split-Path -Parent $PSScriptRoot) 'mod\ModInfos.json'
$text = [IO.File]::ReadAllText($path, [Text.Encoding]::UTF8)
$re = '("Version"\s*:\s*")[^"]*(")'
if (([regex]::Matches($text, $re)).Count -ne 1) { throw "Expected one `"Version`" in $path" }
$new = [regex]::Replace($text, $re, "`${1}$v`${2}")
[IO.File]::WriteAllText($path, $new, (New-Object Text.UTF8Encoding $false))
if ((Get-Content $path -Raw -Encoding UTF8 | ConvertFrom-Json).Version -ne $v) { throw "Could not set the version in $path" }
Write-Host "mod\ModInfos.json Version = $v"
