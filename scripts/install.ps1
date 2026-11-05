# Install the built TABPerf into the game's Mods folder (the game files are not changed).
#   powershell -ExecutionPolicy Bypass -File scripts\install.ps1 [-Engine chipmunk|rapier|chipmunk-vulkan|original] [-Renderer dxvk|d3d9]
#     [-NoDxvk] [-GameDir <folder>]
#     [-Uninstall]
# Copies into <game>\Mods\TABPerf:
#   TABPerf.dll, ModInfos.json                       mod\out
#   chipmunk\DXPhysicsNative.dll                     native\out (multi-core Chipmunk2D)
#   rapier\DXPhysicsNative.dll, rapier2d_ffi*.dll    native\out\rapier (Rapier 2D), when built
#   chipmunk-vulkan\DXPhysicsNative.dll              native\out\chipmunk-vulkan (Chipmunk2D with the step on the GPU), when built
#   dxvk\d3d9.dll, LICENSE                           DXVK's 64-bit d3d9.dll (Direct3D 9 on Vulkan, the official release in
#                                                    scripts\lib\dxvk.ps1, downloaded once into native\out\dxvk, checked
#                                                    by SHA-256)
# The mod sends the game's physics calls to the engine chosen by "physicsEngine" in %APPDATA%\TABPerf\engine.json
# (chipmunk by default); -Engine writes that setting.
# An older install put the engine into the game folder itself (DXPhysicsNative.dll, kept DXPhysicsNative.dll.orig):
# the game's own DLL is put back there.
# -Renderer writes "renderer" to engine.json: dxvk (the mod's default) or d3d9 (the system
# Direct3D 9). The former "dxvk" setting is removed. -NoDxvk skips DXVK (no download; the mod then uses
# the system Direct3D 9 when "renderer" is dxvk).
# -Uninstall removes Mods\TABPerf (settings and logs in %APPDATA%\TABPerf stay).
param([ValidateSet('', 'chipmunk', 'rapier', 'chipmunk-vulkan', 'original')][string]$Engine = '', [ValidateSet('', 'dxvk', 'd3d9')][string]$Renderer = '',
  [switch]$NoDxvk, [string]$GameDir = '', [switch]$Uninstall)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'lib\game.ps1')
. (Join-Path $PSScriptRoot 'lib\dxvk.ps1')

$GameDir = Resolve-GameDir $GameDir
Write-Host "Game: $GameDir"
if (Get-Process -Name 'TheyAreBillions' -ErrorAction SilentlyContinue) { throw 'They Are Billions is running - close the game first.' }

$out = Join-Path $repo 'native\out'
$modOut = Join-Path $repo 'mod\out'
# engine -> files (the first "need" ones are required; the AVX2 and AVX-512 builds are optional)
$engines = [ordered]@{
  chipmunk = @{ files = @("$out\DXPhysicsNative.dll"); need = 1 }
  rapier   = @{ files = @("$out\rapier\DXPhysicsNative.dll", "$out\rapier\rapier2d_ffi.dll", "$out\rapier\rapier2d_ffi_avx2.dll", "$out\rapier\rapier2d_ffi_avx512.dll"); need = 2 }
  'chipmunk-vulkan' = @{ files = @("$out\chipmunk-vulkan\DXPhysicsNative.dll"); need = 1 }
}
function Test-Built($e) { -not ($engines[$e].files[0..($engines[$e].need - 1)] | Where-Object { -not (Test-Path $_) }) }
if (-not $Uninstall) {
  foreach ($f in 'TABPerf.dll', 'ModInfos.json') { if (-not (Test-Path (Join-Path $modOut $f))) { throw "Missing mod\out\$f - run scripts\build.ps1 -Target mod first" } }
  if ($engines.Contains($Engine) -and -not (Test-Built $Engine)) { throw "Engine $Engine is not built - run scripts\build.ps1 -Target $Engine first" }
}
$dxvkDll = $null
if (-not $NoDxvk -and -not $Uninstall) {
  try { $dxvkDll = Get-DxvkD3d9 (Join-Path $out 'dxvk') } catch { Write-Warning "DXVK: $($_.Exception.Message) - installing without it (the mod uses the system Direct3D 9)" }
}

$engineDlls = @($engines.Values | ForEach-Object { $_.files } | ForEach-Object { Split-Path -Leaf $_ } | Select-Object -Unique)

# An older install put the engine into the game folder itself and kept the game's DLL as DXPhysicsNative.dll.orig:
# put the game's DLL back (unless Steam has updated it since - then the current one is the game's) and remove the rest.
function Remove-LegacyInstall {
  $dll = Join-Path $GameDir 'DXPhysicsNative.dll'; $orig = "$dll.orig"
  if (-not (Test-Path $orig)) { return }
  if ((Test-Path $dll) -and (Test-FileContains $dll 'DX_SetOption')) { Copy-Item $orig $dll -Force; Write-Host 'Game folder: game physics DLL put back (an older install had replaced it)' }
  Remove-Item $orig
  foreach ($n in $engineDlls) { if ($n -ne 'DXPhysicsNative.dll') { Remove-Item (Join-Path $GameDir $n) -ErrorAction SilentlyContinue } }
}

try {
  Remove-LegacyInstall

  # ---------- mod folder ----------
  $mod = Join-Path $GameDir 'Mods\TABPerf'
  if ($Uninstall) {
    if (Test-Path $mod) { Remove-Item $mod -Recurse -Force; Write-Host "Mod: removed $mod" }
  } else {
    New-Item -ItemType Directory -Force $mod | Out-Null
    Copy-Item (Join-Path $modOut 'TABPerf.dll'), (Join-Path $modOut 'ModInfos.json') $mod -Force
    # Physics DLLs directly in the mod folder (older packages): the loader tries every top-level *.dll as a .NET assembly.
    foreach ($n in $engineDlls) { Remove-Item (Join-Path $mod $n) -ErrorAction SilentlyContinue }
    foreach ($e in $engines.Keys) {
      $dir = Join-Path $mod $e
      if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
      if (-not (Test-Built $e)) { Write-Host "Engine ${e}: not built (skipped)"; continue }
      $files = @($engines[$e].files | Where-Object { Test-Path $_ })
      New-Item -ItemType Directory $dir | Out-Null
      Copy-Item $files $dir
      Write-Host "Engine ${e}: $(($files | ForEach-Object { Split-Path -Leaf $_ }) -join ', ')"
    }
    $dxvkDir = Join-Path $mod 'dxvk'
    if ($dxvkDll) { New-Item -ItemType Directory -Force $dxvkDir | Out-Null; Copy-Item $dxvkDll, (Join-Path (Split-Path $dxvkDll) 'LICENSE') $dxvkDir -Force; Write-Host "DXVK: $DxvkVersion (dxvk\d3d9.dll)" }
    elseif (Test-Path $dxvkDir) { Remove-Item $dxvkDir -Recurse -Force; Write-Host 'DXVK: removed' }
    Write-Host "Mod: $mod"
  }
}
catch [UnauthorizedAccessException] { throw "No write access to $GameDir - run this script from an administrator PowerShell." }

# ---------- engine and renderer choice (engine.json) ----------
$cfgDir = Join-Path $env:APPDATA 'TABPerf'; $cfg = Join-Path $cfgDir 'engine.json'
$o = if (Test-Path $cfg) { Get-Content $cfg -Raw | ConvertFrom-Json }
$legacy = $null -ne $o -and $o.PSObject.Properties.Name -contains 'dxvk'   # replaced by "renderer"
if (($Engine -or $Renderer -or $legacy) -and -not $Uninstall) {
  if ($null -eq $o) { $o = New-Object PSObject }
  if ($Engine) { $o | Add-Member -NotePropertyName physicsEngine -NotePropertyValue $Engine -Force }
  if ($Renderer) { $o | Add-Member -NotePropertyName renderer -NotePropertyValue $Renderer -Force }
  if ($legacy) { $o.PSObject.Properties.Remove('dxvk'); Write-Host 'engine.json: removed the former "dxvk" setting (now "renderer", default dxvk)' }
  New-Item -ItemType Directory -Force $cfgDir | Out-Null
  [IO.File]::WriteAllText($cfg, ($o | ConvertTo-Json), (New-Object Text.UTF8Encoding $false))
  if ($Engine) { Write-Host "Engine: $Engine ($cfg)" }
  if ($Renderer) { Write-Host "Renderer: $Renderer ($cfg)" }
}

if (-not $Uninstall -and -not (Test-LoaderPatched (Join-Path $GameDir $TabExe))) {
  Write-Host 'Note: the mod needs the TABModLoader-patched game executable (scripts\setup.ps1).'
}
Write-Host 'OK. %APPDATA%\TABPerf\billions.log shows the physics DLL in use (last line of the mod install).'
