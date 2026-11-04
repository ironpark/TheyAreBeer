# Game folder and mod loader helpers (dot-source: . "$PSScriptRoot\lib\game.ps1").

$TabExe = 'TheyAreBillions.exe'
$TabSteamAppId = 644930

function Hash($path) { (Get-FileHash $path -Algorithm SHA256).Hash }

# Whether a (binary) file contains an ASCII string.
function Test-FileContains($path, [string]$text) { [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($path)).Contains($text) }

# The TABModLoader-patched executable carries the loader's assembly (AX.ModLoader); the original game executable does not.
function Test-LoaderPatched($path) { Test-FileContains $path 'AX.ModLoader' }

# They Are Billions installation folder from the Steam library folders, or $null.
function Find-Game {
  $roots = @()
  foreach ($k in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam') {
    $p = Get-ItemProperty $k -ErrorAction SilentlyContinue
    foreach ($v in $p.SteamPath, $p.InstallPath) { if ($v) { $roots += ($v -replace '/', '\') } }
  }
  $libs = @()
  foreach ($r in ($roots | Select-Object -Unique)) {
    $libs += $r
    $vdf = Join-Path $r 'steamapps\libraryfolders.vdf'
    if (Test-Path $vdf) { foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) { $libs += ($m.Groups[1].Value -replace '\\', '\') } }
  }
  foreach ($lib in ($libs | Select-Object -Unique)) {
    $acf = Join-Path $lib "steamapps\appmanifest_$TabSteamAppId.acf"
    $dir = $null
    if ((Test-Path $acf) -and ((Get-Content $acf -Raw) -match '"installdir"\s+"([^"]+)"')) { $dir = Join-Path $lib ('steamapps\common\' + $Matches[1]) }
    elseif (Test-Path (Join-Path $lib 'steamapps\common\They Are Billions')) { $dir = Join-Path $lib 'steamapps\common\They Are Billions' }
    if ($dir -and (Test-Path (Join-Path $dir $TabExe))) { return $dir }
  }
  return $null
}

# -GameDir checked, or the Steam installation; throws when neither has the game.
function Resolve-GameDir([string]$GameDir) {
  if ($GameDir) {
    if (-not (Test-Path (Join-Path $GameDir $TabExe))) { throw "$TabExe not found in -GameDir $GameDir" }
    return $GameDir
  }
  $found = Find-Game
  if (-not $found) { throw 'They Are Billions installation not found - pass -GameDir <folder>.' }
  return $found
}
