# Package the built mod for release: dist\TABPerf-v<version>.zip (version from mod\ModInfos.json).
#   powershell -ExecutionPolicy Bypass -File scripts\package.ps1 [-OutDir dist]
# Needs everything built (scripts\build.ps1: chipmunk, rapier with its three builds, mod). The zip holds one folder,
# TABPerf\, laid out like scripts\install.ps1 installs it (extract it into <game>\Mods):
#   TABPerf.dll, ModInfos.json      the engine mod
#   chipmunk\                       multi-core Chipmunk2D physics DLL
#   rapier\                         Rapier 2D physics DLL and rapier2d_ffi*.dll
#   dxvk\d3d9.dll, LICENSE          DXVK (scripts\lib\dxvk.ps1, downloaded once into native\out\dxvk)
#   licenses\                       this patch (MIT), Chipmunk2D (MIT), Rapier (Apache-2.0), DXVK (zlib)
#   README.txt                      how to install and the main settings
param([string]$OutDir = '')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'lib\dxvk.ps1')
$out = Join-Path $repo 'native\out'
if (-not $OutDir) { $OutDir = Join-Path $repo 'dist' }

$version = (Get-Content (Join-Path $repo 'mod\ModInfos.json') -Raw | ConvertFrom-Json).Version
$files = [ordered]@{
  '.'        = @("$repo\mod\out\TABPerf.dll", "$repo\mod\out\ModInfos.json")
  'chipmunk' = @("$out\DXPhysicsNative.dll")
  'rapier'   = @("$out\rapier\DXPhysicsNative.dll", "$out\rapier\rapier2d_ffi.dll", "$out\rapier\rapier2d_ffi_avx2.dll", "$out\rapier\rapier2d_ffi_avx512.dll")
}
foreach ($f in $files.Values | ForEach-Object { $_ }) { if (-not (Test-Path $f)) { throw "Missing $f - run scripts\build.ps1 first (all targets)" } }
$rapierLicense = Join-Path $out 'rapier-src\LICENSE'
if (-not (Test-Path $rapierLicense)) { throw "Missing $rapierLicense - run scripts\build.ps1 -Target rapier first" }
$dxvk = Get-DxvkD3d9 (Join-Path $out 'dxvk')

$stage = Join-Path $OutDir 'stage'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
$root = Join-Path $stage 'TABPerf'
foreach ($sub in $files.Keys) {
  $dir = if ($sub -eq '.') { $root } else { Join-Path $root $sub }
  New-Item -ItemType Directory -Force $dir | Out-Null
  Copy-Item $files[$sub] $dir
}
New-Item -ItemType Directory -Force (Join-Path $root 'dxvk'), (Join-Path $root 'licenses') | Out-Null
Copy-Item $dxvk, (Join-Path (Split-Path $dxvk) 'LICENSE') (Join-Path $root 'dxvk')
Copy-Item (Join-Path $repo 'LICENSE.txt') (Join-Path $root 'licenses\TABPerf-MIT.txt')
Copy-Item (Join-Path $repo 'native\chipmunk\LICENSE.txt') (Join-Path $root 'licenses\Chipmunk2D-MIT.txt')
Copy-Item $rapierLicense (Join-Path $root 'licenses\Rapier-Apache-2.0.txt')
Copy-Item (Join-Path (Split-Path $dxvk) 'LICENSE') (Join-Path $root 'licenses\DXVK-zlib.txt')
# README.txt from scripts\package-readme.txt ({version} filled in); the template stays out of this script so that it
# remains ASCII (Windows PowerShell reads scripts without a BOM in the system code page)
$readme = (Get-Content (Join-Path $PSScriptRoot 'package-readme.txt') -Raw -Encoding UTF8).Replace('{version}', $version)
[IO.File]::WriteAllText((Join-Path $root 'README.txt'), ($readme -replace "`r?`n", "`r`n"), (New-Object Text.UTF8Encoding $true))

New-Item -ItemType Directory -Force $OutDir | Out-Null
$zip = Join-Path $OutDir "TABPerf-v$version.zip"
if (Test-Path $zip) { Remove-Item $zip }
# entry names with '/' separators, given explicitly (Compress-Archive and ZipFile.CreateFromDirectory in Windows
# PowerShell 5.1 write '\', which other unzip tools take as part of the file name)
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
try {
  foreach ($f in Get-ChildItem $stage -Recurse -File) {
    $name = $f.FullName.Substring($stage.Length).TrimStart('\').Replace('\', '/')
    [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $f.FullName, $name, [IO.Compression.CompressionLevel]::Optimal)
  }
}
finally { $archive.Dispose() }
Remove-Item $stage -Recurse -Force
Write-Host "OK: $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
