# Set up a machine for building and running TABPerf, using TABModLoader (https://codeberg.org/JKstring/TABModLoader).
#   powershell -ExecutionPolicy Bypass -File scripts\setup.ps1 [-GameDir <folder>] [-RefDir <folder>] [-Version v1.1.7|latest] [-NoPatch] [-Force]
# 1. Finds the They Are Billions installation (Steam library folders, or -GameDir).
# 2. Downloads the build references into <repo>\refs (ignored by git):
#      TheyAreBillions.exe          - the TABModLoader-patched game executable (ModLoaderUpdate.zip of the release, renamed)
#      DXVision.dll, 0Harmony.dll   - from the loader repository's Lib\<version> folder
# 3. Checks whether the installed game executable is patched by the mod loader and, unless -NoPatch, patches it:
#      the original is kept as TheyAreBillions.exe.orig next to it (restore it, or verify the game files in Steam, to undo).
# Then build with scripts\build.ps1 and install with scripts\install.ps1.
param([string]$GameDir = '', [string]$RefDir = '', [string]$Version = '', [switch]$NoPatch, [switch]$Force)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

$repo = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'lib\game.ps1')
if (-not $RefDir) { $RefDir = Join-Path $repo 'refs' }
$api = 'https://codeberg.org/api/v1/repos/JKstring/TABModLoader'
$exeName = $TabExe
# the loader release the mod is built and tested against (the mod also runs on v1.1.6); -Version latest takes the
# newest release
if (-not $Version) { $Version = 'v1.1.7' }

# ---------- 1. game installation ----------
if ($GameDir) {
  if (-not (Test-Path (Join-Path $GameDir $exeName))) { throw "$exeName not found in -GameDir $GameDir" }
} else {
  $GameDir = Find-Game
}
if ($GameDir) { Write-Host "Game: $GameDir" }
elseif (-not $NoPatch) { throw 'They Are Billions installation not found - pass -GameDir <folder>, or -NoPatch to only download the build references.' }
else { Write-Host 'Game: not found (skipped, -NoPatch)' }
$gameExe = if ($GameDir) { Join-Path $GameDir $exeName } else { $null }

# ---------- 2. what needs doing ----------
$refFiles = $exeName, 'DXVision.dll', '0Harmony.dll'
$refsMissing = [bool]($refFiles | Where-Object { -not (Test-Path (Join-Path $RefDir $_)) })
$refExe = Join-Path $RefDir $exeName
$gamePatchedToRef = $gameExe -and -not $refsMissing -and ((Hash $gameExe) -eq (Hash $refExe))
$needPatchCheck = $gameExe -and -not $NoPatch -and -not $gamePatchedToRef
if (-not $Force -and -not $refsMissing -and -not $needPatchCheck) {
  Write-Host "References: up to date in $RefDir (use -Force to download again)"
  if ($gamePatchedToRef) { Write-Host 'Mod loader: patched (same loader build as the references)' }
  elseif ($gameExe) { Write-Host "Mod loader: $(if (Test-LoaderPatched $gameExe) { 'patched with a different loader build' } else { 'NOT patched' }) (left as is, -NoPatch)" }
  return
}

# ---------- 3. download the loader release ----------
$release = if ($Version -eq 'latest') { Invoke-RestMethod "$api/releases/latest" } else { Invoke-RestMethod "$api/releases/tags/$Version" }
$asset = $release.assets | Where-Object { $_.name -eq 'ModLoaderUpdate.zip' } | Select-Object -First 1
if (-not $asset) { throw "ModLoaderUpdate.zip not found in release $($release.tag_name)" }
Write-Host "TABModLoader release $($release.tag_name)"

$tmp = Join-Path ([IO.Path]::GetTempPath()) ('tabml-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $tmp | Out-Null
try {
  $zip = Join-Path $tmp 'ModLoaderUpdate.zip'
  Invoke-WebRequest $asset.browser_download_url -OutFile $zip -UseBasicParsing
  Expand-Archive $zip -DestinationPath (Join-Path $tmp 'x')
  $loaderExe = Get-ChildItem (Join-Path $tmp 'x') -Recurse -Filter 'TheyAreBillions_ModLoaderUpdate.exe' | Select-Object -First 1
  if (-not $loaderExe) { throw 'TheyAreBillions_ModLoaderUpdate.exe not found in ModLoaderUpdate.zip' }
  if (-not (Test-LoaderPatched $loaderExe.FullName)) { throw 'Downloaded executable does not contain the mod loader (AX.ModLoader) - refusing to use it' }
  $loaderHash = Hash $loaderExe.FullName

  # ---------- 4. build references ----------
  if ($Force -or $refsMissing -or (Hash $refExe) -ne $loaderHash) {
    New-Item -ItemType Directory -Force $RefDir | Out-Null
    Copy-Item $loaderExe.FullName $refExe -Force
    # each library from the newest Lib\v* folder, up to the release's version, that has it (a loader release only adds
    # the libraries it changed: Lib\v1.1.7 holds DXVision.dll alone, 0Harmony.dll is in Lib\v1.1.0)
    # (parentheses: Windows PowerShell 5.1 sends a JSON array down the pipeline as one object)
    $relVersion = [version]$release.tag_name.TrimStart('v')
    $libs = (Invoke-RestMethod "$api/contents/Lib") | Where-Object { $_.type -eq 'dir' -and $_.name -match '^v\d+(\.\d+)*$' } |
      Where-Object { [version]($_.name.Substring(1)) -le $relVersion } | Sort-Object { [version]($_.name.Substring(1)) } -Descending
    if (-not $libs) { throw 'No Lib\v* folder found in the TABModLoader repository' }
    $listings = @{}
    $from = @()
    foreach ($name in 'DXVision.dll', '0Harmony.dll') {
      $e = $null
      foreach ($lib in $libs) {
        if (-not $listings.ContainsKey($lib.path)) { $listings[$lib.path] = Invoke-RestMethod "$api/contents/$($lib.path)" }
        $e = @($listings[$lib.path]) | Where-Object { $_.name -eq $name } | Select-Object -First 1
        if ($e) { break }
      }
      if (-not $e) { throw "$name not found in any Lib\v* folder of the TABModLoader repository" }
      Invoke-WebRequest $e.download_url -OutFile (Join-Path $RefDir $name) -UseBasicParsing
      $from += "$name from $($lib.path)"
    }
    Write-Host "References: $RefDir ($($from -join ', '))"
    foreach ($f in $refFiles) { $p = Join-Path $RefDir $f; Write-Host ('  {0,-20} {1,10:N0} bytes  SHA256 {2}' -f $f, (Get-Item $p).Length, (Hash $p)) }
  } else { Write-Host "References: up to date in $RefDir" }

  # The mod declares the loader version it needs; warn when the downloaded loader is a different minor version.
  $need = (Get-Content (Join-Path $repo 'mod\ModInfos.json') -Raw | ConvertFrom-Json).RequiredModLoaderVersion
  $have = $release.tag_name.TrimStart('v')
  if ($need -and (([version]$need).Major -ne ([version]$have).Major -or ([version]$need).Minor -ne ([version]$have).Minor)) {
    Write-Warning "mod\ModInfos.json requires loader $need, downloaded $have"
  }

  # ---------- 5. mod loader patch ----------
  if ($gameExe) {
    $gameHash = Hash $gameExe
    $patched = Test-LoaderPatched $gameExe
    if ($gameHash -eq $loaderHash) { Write-Host "Mod loader: patched ($($release.tag_name))" }
    elseif ($NoPatch) { Write-Host "Mod loader: $(if ($patched) { 'patched with a different loader build' } else { 'NOT patched' }) (left as is, -NoPatch)" }
    else {
      if (Get-Process -Name 'TheyAreBillions' -ErrorAction SilentlyContinue) { throw 'They Are Billions is running - close the game and run setup again.' }
      $backup = "$gameExe.orig"
      try {
        if ($patched) {
          Write-Host "Mod loader: patched with a different loader build - updating to $($release.tag_name)"
        } else {
          # Keep the original executable. A different existing backup means Steam updated the game since: keep the newer original.
          if (-not (Test-Path $backup) -or (Hash $backup) -ne $gameHash) { Copy-Item $gameExe $backup -Force; Write-Host "Mod loader: NOT patched - original saved as $backup" }
          else { Write-Host 'Mod loader: NOT patched - original already saved' }
        }
        Copy-Item $loaderExe.FullName $gameExe -Force
      }
      catch [UnauthorizedAccessException] { throw "No write access to $GameDir - run this script from an administrator PowerShell, or pass -NoPatch." }
      if ((Hash $gameExe) -ne $loaderHash) { throw "Patching failed: $gameExe does not match the loader executable" }
      Write-Host "Mod loader: patched ($($release.tag_name)). Steam 'Verify integrity of game files' or a game update undoes it - run setup again then."
    }
    $mods = Join-Path $GameDir 'Mods'
    if (-not (Test-Path $mods)) { Write-Host "Mods folder: created by the loader on the first game start ($mods)" }
  }
}
finally { Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue }

Write-Host 'OK. Next: scripts\build.ps1, then scripts\install.ps1'
