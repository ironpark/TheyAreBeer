# Install the built TABPerf into the game's Mods folder (the game files are not changed).
#   powershell -ExecutionPolicy Bypass -File scripts\install.ps1 [-Engine chipmunk|rapier|original] [-GameDir <folder>] [-Uninstall]
# Copies into <game>\Mods\TABPerf:
#   TABPerf.dll, ModInfos.json                       mod\out
#   chipmunk\DXPhysicsNative.dll                     native\out (multi-core Chipmunk2D)
#   rapier\DXPhysicsNative.dll, rapier2d_ffi*.dll    native\out\rapier (Rapier 2D), when built
# The mod sends the game's physics calls to the engine chosen by "physicsEngine" in %APPDATA%\TABPerf\engine.json
# (chipmunk by default); -Engine writes that setting.
# An older install put the engine into the game folder itself (DXPhysicsNative.dll, kept DXPhysicsNative.dll.orig):
# the game's own DLL is put back there.
# -Uninstall removes Mods\TABPerf (settings and logs in %APPDATA%\TABPerf stay).
param([ValidateSet('', 'chipmunk', 'rapier', 'original')][string]$Engine = '', [string]$GameDir = '', [switch]$Uninstall)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'lib\game.ps1')

$GameDir = Resolve-GameDir $GameDir
Write-Host "Game: $GameDir"
if (Get-Process -Name 'TheyAreBillions' -ErrorAction SilentlyContinue) { throw 'They Are Billions is running - close the game first.' }

$out = Join-Path $repo 'native\out'
$modOut = Join-Path $repo 'mod\out'
# engine -> files (the first "need" ones are required; the AVX2 build is optional)
$engines = [ordered]@{
  chipmunk = @{ files = @("$out\DXPhysicsNative.dll"); need = 1 }
  rapier   = @{ files = @("$out\rapier\DXPhysicsNative.dll", "$out\rapier\rapier2d_ffi.dll", "$out\rapier\rapier2d_ffi_avx2.dll"); need = 2 }
}
function Test-Built($e) { -not ($engines[$e].files[0..($engines[$e].need - 1)] | Where-Object { -not (Test-Path $_) }) }
if (-not $Uninstall) {
  foreach ($f in 'TABPerf.dll', 'ModInfos.json') { if (-not (Test-Path (Join-Path $modOut $f))) { throw "Missing mod\out\$f - run scripts\build.ps1 -Target mod first" } }
  if ($engines.Contains($Engine) -and -not (Test-Built $Engine)) { throw "Engine $Engine is not built - run scripts\build.ps1 -Target $Engine first" }
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
    Write-Host "Mod: $mod"
  }
}
catch [UnauthorizedAccessException] { throw "No write access to $GameDir - run this script from an administrator PowerShell." }

# ---------- engine choice (engine.json) ----------
if ($Engine -and -not $Uninstall) {
  $cfgDir = Join-Path $env:APPDATA 'TABPerf'; $cfg = Join-Path $cfgDir 'engine.json'
  $o = if (Test-Path $cfg) { Get-Content $cfg -Raw | ConvertFrom-Json }
  if ($null -eq $o) { $o = New-Object PSObject }
  $o | Add-Member -NotePropertyName physicsEngine -NotePropertyValue $Engine -Force
  New-Item -ItemType Directory -Force $cfgDir | Out-Null
  [IO.File]::WriteAllText($cfg, ($o | ConvertTo-Json), (New-Object Text.UTF8Encoding $false))
  Write-Host "Engine: $Engine ($cfg)"
}

if (-not $Uninstall -and -not (Test-LoaderPatched (Join-Path $GameDir $TabExe))) {
  Write-Host 'Note: the mod needs the TABModLoader-patched game executable (scripts\setup.ps1).'
}
Write-Host 'OK. %APPDATA%\TABPerf\billions.log shows the physics DLL in use (last line of the mod install).'
