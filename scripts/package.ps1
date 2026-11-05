# Package the built mod for release: dist\TABPerf-v<version>.zip (version from mod\ModInfos.json) and dist\ModPackage.zip
# (the same files without the TABPerf\ folder, the release asset TABModLoader's auto-update installs).
#   powershell -ExecutionPolicy Bypass -File scripts\package.ps1 [-OutDir dist]
# Needs everything built (scripts\build.ps1: chipmunk, rapier with its three builds, chipmunk-vulkan, mod). The zip holds one folder,
# TABPerf\, laid out like scripts\install.ps1 installs it (extract it into <game>\Mods):
#   TABPerf.dll, ModInfos.json      the engine mod
#   chipmunk\                       multi-core Chipmunk2D physics DLL
#   rapier\                         Rapier 2D physics DLL and rapier2d_ffi*.dll
#   chipmunk-vulkan\                Chipmunk2D physics DLL with the step on the GPU (Vulkan compute)
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
  'chipmunk-vulkan' = @("$out\chipmunk-vulkan\DXPhysicsNative.dll")
}
foreach ($f in $files.Values | ForEach-Object { $_ }) { if (-not (Test-Path $f)) { throw "Missing $f - run scripts\build.ps1 first (all targets)" } }
$rapierLicense = Join-Path $out 'rapier-sdk\LICENSE'   # copied there by the Rapier build (scripts\build.ps1)
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
# Chipmunk2DVulkan is built from the upstream repository (copied there by the chipmunk-vulkan build)
Copy-Item (Join-Path $out 'chipmunk-vulkan\Chipmunk2D-LICENSE.txt') (Join-Path $root 'licenses\Chipmunk2D-upstream-MIT.txt')
Copy-Item (Join-Path (Split-Path $dxvk) 'LICENSE') (Join-Path $root 'licenses\DXVK-zlib.txt')
# README.txt from scripts\package-readme.txt ({version} filled in); the template stays out of this script so that it
# remains ASCII (Windows PowerShell reads scripts without a BOM in the system code page)
$readme = (Get-Content (Join-Path $PSScriptRoot 'package-readme.txt') -Raw -Encoding UTF8).Replace('{version}', $version)
[IO.File]::WriteAllText((Join-Path $root 'README.txt'), ($readme -replace "`r?`n", "`r`n"), (New-Object Text.UTF8Encoding $true))

New-Item -ItemType Directory -Force $OutDir | Out-Null
# entry names with '/' separators, given explicitly (Compress-Archive and ZipFile.CreateFromDirectory in Windows
# PowerShell 5.1 write '\', which other unzip tools take as part of the file name)
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
function Write-Zip([string]$zip, [string]$dir) {
  if (Test-Path $zip) { Remove-Item $zip }
  $archive = [IO.Compression.ZipFile]::Open($zip, [IO.Compression.ZipArchiveMode]::Create)
  try {
    foreach ($f in Get-ChildItem $dir -Recurse -File) {
      $name = $f.FullName.Substring($dir.Length).TrimStart('\').Replace('\', '/')
      [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $f.FullName, $name, [IO.Compression.CompressionLevel]::Optimal)
    }
  }
  finally { $archive.Dispose() }
  Write-Host "OK: $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
}
# TABPerf-v<version>.zip: the TABPerf\ folder, extracted by hand into <game>\Mods
Write-Zip (Join-Path $OutDir "TABPerf-v$version.zip") $stage
# ModPackage.zip: the folder's contents at the root - TABModLoader's auto-update (ModInfos.json "RepositoryUrl",
# "AllowAutoUpdate") downloads this release asset, empties the mod folder and extracts it there
Write-Zip (Join-Path $OutDir 'ModPackage.zip') $root
Remove-Item $stage -Recurse -Force
