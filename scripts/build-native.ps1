# Build DXPhysicsNative.dll (multi-core Chipmunk 7.0.0 physics for They Are Billions).
# Requires Visual Studio 2019/2022 (or Build Tools) with "Desktop development with C++" (MSVC x64).
#   powershell -ExecutionPolicy Bypass -File scripts\build-native.ps1
# Output: native\out\DXPhysicsNative.dll
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$root = Join-Path $repo 'native'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found - install Visual Studio Build Tools with the C++ workload.' }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC x64 tools not found.' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found: $vcvars" }
if ($root -match ' ') { throw "Path must not contain spaces (needed for /d1trimfile): $root" }
$out = Join-Path $root 'out'; $obj = Join-Path $out 'obj'; New-Item -ItemType Directory -Force $out, $obj | Out-Null
# Remove the previous DLL first so a failed build never leaves a stale one behind.
$dll = Join-Path $out 'DXPhysicsNative.dll'; $map = Join-Path $out 'DXPhysicsNative.map'
Remove-Item $dll, $map -ErrorAction SilentlyContinue
$src = Get-ChildItem (Join-Path $root 'chipmunk\src') -Filter *.c | Where-Object { $_.Name -ne 'cpHastySpace.c' } | ForEach-Object { '"chipmunk\src\' + $_.Name + '"' }
$src += '"dxphysics\DXPhysicsNative.c"', '"dxphysics\dxpool.c"', '"dxphysics\dxdiag.c"'
# /fp:precise /fp:contract- : same float operation order as the original DLL (needed for bit-identical results with the parallel switches off).
# No /arch flag: SSE2 baseline, runs on any x64 CPU. /MT: static C runtime, no redistributable needed. /d1trimfile: keep build paths out of the DLL.
$flags = '/nologo /utf-8 /O2 /MT /LD /W3 /fp:precise /fp:contract- /DNDEBUG /DWIN32 /DCP_USE_DOUBLES=0 /D_CRT_SECURE_NO_WARNINGS /wd4244 /wd4305 /wd4018 /d1trimfile:' + $root + '\'
$cmd = "cd /d `"$root`" && `"$vcvars`" >nul && cl $flags /I`"chipmunk\include`" /I`"dxphysics`" /Fo`"$obj\\`" /Fe`"$out\DXPhysicsNative.dll`" $($src -join ' ') /link /NOLOGO /MAP:`"$map`""
$log = Join-Path $out 'build.log'
$ErrorActionPreference = 'Continue'
cmd /c $cmd *> $log
if ($LASTEXITCODE -ne 0) { Get-Content $log | Select-String -Pattern 'error' | Select-Object -First 20; throw "Build failed (see $log)" }
# TABModLoader overwrites 5 bytes at DLL base + 0x13B3; that range must lie inside dx_text_pad (8 KB of nops, dxdiag.c).
$base = $null; $pad = $null
foreach ($line in Get-Content $map) {
  if ($line -match 'Preferred load address is ([0-9A-Fa-f]+)') { $base = [Convert]::ToUInt64($Matches[1], 16) }
  elseif ($line -match '^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+dx_text_pad\s+([0-9A-Fa-f]+)\s') { $pad = [Convert]::ToUInt64($Matches[1], 16) }
}
if ($null -eq $base -or $null -eq $pad) { Remove-Item $dll; throw "dx_text_pad not found in $map" }
$padRva = $pad - $base
if ($padRva -gt 0x13B3 -or ($padRva + 8192) -lt 0x13B8) { Remove-Item $dll; throw ('dx_text_pad at RVA 0x{0:X} does not cover the loader patch at 0x13B3' -f $padRva) }
Write-Host ("OK: $dll  (dx_text_pad at RVA 0x{0:X})" -f $padRva)
# Optional: comparison harness (runs the original and the new DLL side by side and compares every body bit by bit).
$h = Join-Path $root 'harness\compare.c'
if (Test-Path $h) {
  cmd /c "cd /d `"$root`" && `"$vcvars`" >nul && cl /nologo /utf-8 /O2 /MT /W3 /D_CRT_SECURE_NO_WARNINGS /Fo`"$obj\\`" /Fe`"$out\compare.exe`" harness\compare.c /link /NOLOGO" *> (Join-Path $out 'build-harness.log')
  if ($LASTEXITCODE -eq 0) { Write-Host "OK: $out\compare.exe  (usage: compare.exe <original DLL> <new DLL> [scene|all] [bodies] [steps])" }
  else { Write-Warning "compare.exe build failed (see $(Join-Path $out 'build-harness.log'))" }
}
