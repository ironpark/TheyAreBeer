# In-game benchmark: the same fixed scenario on the unmodded game, the first release of the mod and the current mod.
#   powershell -ExecutionPolicy Bypass -File scripts\bench.ps1 [-Target original,first,chipmunk,rapier,chipmunk-vulkan] [-Dxvk] [-Zombies 100000]
#     [-OriginalZombies 50000] [-Runs 1] [-Factor 4] [-Delay 5] [-Seed 20261104] [-Timeout 3600] [-Zoom max|off] [-Diag] [-Threads -1] [-Counters]
#     [-OutDir <folder>] [-GameDir <folder>]
#
# Targets:
#   original  the game with no optimization mod (Mods\TABPerf moved out of the Mods folder for the run)
#   first     the first release of the mod (commit ef035e1): its TABPerf.dll in Mods\TABPerf and its multi-core Chipmunk
#             DXPhysicsNative.dll in the game folder, as its install script did (the game's DLL is put back afterwards).
#             Built once from git into native\out\first-release.
#   chipmunk  the current mod, Chipmunk multi-core engine (the installed Mods\TABPerf)
#   rapier    the current mod, Rapier engine
#   chipmunk-vulkan  the current mod, Chipmunk2DVulkan engine (step on the GPU)
# -Dxvk runs chipmunk and rapier with DXVK (the original and the first release cannot load it); without it they use the
# system Direct3D 9. -Renderer dxvk|d3d9 picks the current mod's renderer directly (-Renderer dxvk = -Dxvk).
#
# Every run uses the same measuring mod, TABBench (bench\out, scripts\build.ps1 -Target bench), installed into
# Mods\TABBench for the runs only: it starts a fixed survival game (seed -Seed) zoomed out as far as the game allows,
# turns damage off and triggers the final swarm -Factor times -Delay seconds into the level, writes one CSV row per
# second and quits the game once -Zombies zombies are alive (-OriginalZombies for the unmodded game, which gets
# very slow with many zombies), or after -Timeout seconds. See bench\TABBench.cs for what
# is measured and how its own work is kept out of the frame times.
# -Diag also turns on the current mod's diagnostic recording (<target>-diag-<run>.csv, docs\BENCH.md); it adds timers
# on hot paths, so the main CSV of such a run is not comparable. -Threads / -Counters only apply with -Diag.
# -Terrain counts the zombies standing on inaccessible map cells (water, cliffs) every second (zombies_in_terrain); it
# walks every zombie under the logic lock, so use it for correctness runs, not timing runs.
#
# Results: <OutDir>\<target>-<run>.csv and summary.csv; default OutDir bench-out\<date-time> in the repo.
# Needs the current mod installed (scripts\install.ps1) and Steam running. The game normally restarts itself through
# Steam, which would drop the environment variables; a steam_appid.txt in the game folder makes it run directly. The
# script creates that file for the runs and removes it afterwards (one that was already there is kept). Everything the
# script changes in the game folder is put back at the end, also when a run fails.
param(
  [string[]]$Target = @('original', 'first', 'chipmunk', 'rapier'),
  [switch]$Dxvk,
  [ValidateSet('', 'dxvk', 'd3d9')][string]$Renderer = '',
  [int]$Zombies = 100000,
  [int]$OriginalZombies = 50000,
  [int]$Runs = 1,
  [int]$Factor = 4,
  [int]$Delay = 5,
  [int]$Seed = 20261104,
  [int]$Timeout = 3600,
  [ValidateSet('max', 'off')][string]$Zoom = 'max',
  [switch]$Diag,
  [switch]$Terrain,
  [int]$Threads = -1,
  [switch]$Counters,
  [string]$OutDir = '',
  [string]$GameDir = ''
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\game.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$FirstCommit = 'ef035e1'

# -File passes "-Target a,b" as one string: split it here
$Target = @($Target | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim().ToLowerInvariant() } | Where-Object { $_ })
$known = 'original', 'first', 'chipmunk', 'rapier', 'chipmunk-vulkan'
$currentEngines = 'chipmunk', 'rapier', 'chipmunk-vulkan'   # targets that run the current mod with one of its engines
foreach ($t in $Target) { if ($known -notcontains $t) { throw "Unknown -Target $t (use $($known -join ', '))" } }
if ($Runs -lt 1) { throw '-Runs must be 1 or more' }

$GameDir = Resolve-GameDir $GameDir
$exe = Join-Path $GameDir $TabExe
$mods = Join-Path $GameDir 'Mods'
$mod = Join-Path $mods 'TABPerf'
$benchMod = Join-Path $mods 'TABBench'
$saved = Join-Path $GameDir 'TABPerf.bench-saved'          # the installed mod while original / first run
$gameDll = Join-Path $GameDir 'DXPhysicsNative.dll'
$gameDllSaved = Join-Path $GameDir 'DXPhysicsNative.dll.bench-saved'
$benchOut = Join-Path $repo 'bench\out'
foreach ($p in $saved, $gameDllSaved, $benchMod) { if (Test-Path $p) { throw "$p is left from an interrupted benchmark - put it back (or remove it) first" } }
if (-not (Test-Path (Join-Path $benchOut 'TABBench.dll'))) { throw 'TABBench is not built - run scripts\build.ps1 -Target bench' }
if (-not (Test-Path (Join-Path $mod 'TABPerf.dll'))) { throw "TABPerf is not installed in $mod - run scripts\install.ps1 first" }
foreach ($e in $currentEngines) {
  if ($Target -contains $e -and -not (Test-Path (Join-Path $mod "$e\DXPhysicsNative.dll"))) { throw "Engine $e is not installed in $mod\$e - run scripts\build.ps1 -Target $e, then scripts\install.ps1" }
}
if (Get-Process ($TabExe -replace '\.exe$', '') -ErrorAction SilentlyContinue) { throw 'The game is already running - close it first.' }
if (-not (Get-Process steam -ErrorAction SilentlyContinue)) { Write-Warning 'Steam does not seem to be running; the game needs it to start.' }

# DXVK for the current mod: the installed one, else the copy install.ps1 downloaded (put in for the runs only)
$dxvkDir = Join-Path $mod 'dxvk'; $dxvkAdded = $false
if (($Dxvk -or $Renderer -eq 'dxvk') -and @($Target | Where-Object { $currentEngines -contains $_ }).Count -and -not (Test-Path (Join-Path $dxvkDir 'd3d9.dll'))) {
  $d = Join-Path $repo 'native\out\dxvk\d3d9.dll'
  if (-not (Test-Path $d)) { throw 'DXVK is not available - run scripts\install.ps1 once (it downloads it), or leave -Dxvk out' }
}

# The first release, built once from its commit (its own build scripts) into native\out\first-release
function Get-FirstRelease {
  $dir = Join-Path $repo 'native\out\first-release'
  $files = 'TABPerf.dll', 'ModInfos.json', 'DXPhysicsNative.dll'
  if (@($files | Where-Object { -not (Test-Path (Join-Path $dir $_)) }).Count -eq 0) { return $dir }
  $refs = Join-Path $repo 'refs'
  $src = Join-Path $repo 'native\out\first-release-src'
  Write-Host "Building the first release ($FirstCommit) into $dir"
  if (Test-Path $src) { git -C $repo worktree remove --force $src 2>$null; Remove-Item -Recurse -Force $src -ErrorAction SilentlyContinue }
  git -C $repo worktree add --detach $src $FirstCommit | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "git worktree add $FirstCommit failed" }
  try {
    & powershell -ExecutionPolicy Bypass -File (Join-Path $src 'build-native.ps1') | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'first release: physics DLL build failed' }
    & powershell -ExecutionPolicy Bypass -File (Join-Path $src 'build-mod.ps1') -RefDir $refs | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'first release: mod build failed' }
    New-Item -ItemType Directory -Force $dir | Out-Null
    Copy-Item (Join-Path $src 'mod\out\TABPerf.dll'), (Join-Path $src 'mod\out\ModInfos.json'), (Join-Path $src 'native\out\DXPhysicsNative.dll') $dir -Force
  }
  finally { git -C $repo worktree remove --force $src | Out-Null }
  return $dir
}
$first = if ($Target -contains 'first') { Get-FirstRelease } else { $null }

if (-not $OutDir) { $OutDir = Join-Path $repo ('bench-out\' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

$vars = 'TABBENCH_CSV', 'TABBENCH_AUTO', 'TABBENCH_SEED', 'TABBENCH_DELAY', 'TABBENCH_FACTOR', 'TABBENCH_ZOMBIES', 'TABBENCH_TIMEOUT', 'TABBENCH_ZOOM', 'TABBENCH_TERRAIN',
        'TABPERF_ENGINE', 'TABPERF_RENDERER', 'TABPERF_BENCH', 'TABPERF_BENCH_CSV', 'TABPERF_BENCH_THREADS', 'DX_RAPIER_COUNTERS'

# Game folder state for one target; Restore-Game puts everything back.
function Set-Game($t) {
  $wantCurrent = $currentEngines -contains $t
  if ($wantCurrent) {
    if (Test-Path $saved) { if (Test-Path $mod) { Remove-Item -Recurse -Force $mod }; Move-Item $saved $mod }
  }
  elseif (-not (Test-Path $saved)) { Move-Item $mod $saved }
  elseif (Test-Path $mod) { Remove-Item -Recurse -Force $mod }   # the first release's folder from the previous run
  if ($t -eq 'first') {
    New-Item -ItemType Directory -Force $mod | Out-Null
    Copy-Item (Join-Path $first 'TABPerf.dll'), (Join-Path $first 'ModInfos.json') $mod -Force
    if (-not (Test-Path $gameDllSaved)) { Copy-Item $gameDll $gameDllSaved }
    Copy-Item (Join-Path $first 'DXPhysicsNative.dll') $gameDll -Force
  }
  elseif (Test-Path $gameDllSaved) { Copy-Item $gameDllSaved $gameDll -Force; Remove-Item $gameDllSaved }
  if ($wantCurrent -and ($Dxvk -or $Renderer -eq 'dxvk') -and -not (Test-Path (Join-Path $dxvkDir 'd3d9.dll'))) {
    New-Item -ItemType Directory -Force $dxvkDir | Out-Null
    Copy-Item (Join-Path $repo 'native\out\dxvk\d3d9.dll') $dxvkDir; $script:dxvkAdded = $true
  }
}
function Restore-Game {
  if (Test-Path $saved) { if (Test-Path $mod) { Remove-Item -Recurse -Force $mod }; Move-Item $saved $mod }
  if (Test-Path $gameDllSaved) { Copy-Item $gameDllSaved $gameDll -Force; Remove-Item $gameDllSaved }
  if ($dxvkAdded) { Remove-Item -Recurse -Force $dxvkDir -ErrorAction SilentlyContinue }
  if (Test-Path $benchMod) { Remove-Item -Recurse -Force $benchMod }
}

# CSV rows of one run -> summary numbers
function Summarize($csv, $t, $run) {
  $inv = [Globalization.CultureInfo]::InvariantCulture
  $rows = @(Import-Csv $csv)
  if (-not $rows.Count) { return $null }
  $swarm = @($rows | Where-Object { $_.note -like 'swarm*' } | Select-Object -First 1)
  $t0 = if ($swarm.Count) { [double]::Parse($swarm[0].t_s, $inv) } else { 0 }
  $last = $rows[-1]
  $live = @($rows | Where-Object { $_.paused -eq '0' -and [int]$_.frames -gt 0 })
  function Avg($set, $col) { $v = @($set | ForEach-Object { [double]::Parse($_.$col, $inv) }); if ($v.Count) { ($v | Measure-Object -Average).Average } else { 0 } }
  $heavy = @($live | Where-Object { [int]$_.zombies -ge 90000 })
  [pscustomobject][ordered]@{
    target = $t; renderer = $(if ($currentEngines -contains $t) { $(if ($Renderer) { $Renderer } elseif ($Dxvk) { 'dxvk' } else { 'd3d9' }) } else { 'd3d9' }); run = $run
    end = $last.note; zombies_max = ($rows | ForEach-Object { [int]$_.zombies } | Measure-Object -Maximum).Maximum
    real_s = [math]::Round([double]::Parse($last.t_s, $inv) - $t0, 0)      # from the swarm to the end of the run
    game_s = [math]::Round([double]::Parse($last.game_time_s, $inv) - $(if ($swarm.Count) { [double]::Parse($swarm[0].game_time_s, $inv) } else { 0 }), 0)
    speed_90k = [math]::Round((Avg $heavy 'game_speed'), 3); frame_ms_90k = [math]::Round((Avg $heavy 'frame_ms'), 1)
    logic_wait_ms_90k = [math]::Round((Avg $heavy 'logic_wait_ms'), 1); cpu_cores_90k = [math]::Round((Avg $heavy 'cpu_cores'), 1)
    bench_ms_per_s = [math]::Round((Avg $live 'bench_ms'), 3)
  }
}

$gameDllHash = Hash $gameDll
$createdAppId = $false
$appId = Join-Path $GameDir 'steam_appid.txt'
$results = @()
try {
  if (-not (Test-Path $appId)) { [IO.File]::WriteAllText($appId, "$TabSteamAppId"); $createdAppId = $true }
  New-Item -ItemType Directory -Force $benchMod | Out-Null
  Copy-Item (Join-Path $benchOut 'TABBench.dll'), (Join-Path $benchOut 'ModInfos.json') $benchMod -Force
  $env:TABBENCH_AUTO = '1'; $env:TABBENCH_SEED = "$Seed"; $env:TABBENCH_DELAY = "$Delay"; $env:TABBENCH_FACTOR = "$Factor"
  $env:TABBENCH_ZOMBIES = "$Zombies"; $env:TABBENCH_TIMEOUT = "$Timeout"; $env:TABBENCH_ZOOM = $Zoom
  if ($Terrain) { $env:TABBENCH_TERRAIN = '1' }
  for ($r = 1; $r -le $Runs; $r++) {
    foreach ($t in $Target) {
      Set-Game $t
      $csv = Join-Path $OutDir "$t-$r.csv"
      Remove-Item $csv -ErrorAction SilentlyContinue
      $env:TABBENCH_CSV = $csv
      $env:TABBENCH_ZOMBIES = $(if ($t -eq 'original') { "$OriginalZombies" } else { "$Zombies" })
      foreach ($v in 'TABPERF_ENGINE', 'TABPERF_RENDERER', 'TABPERF_BENCH', 'TABPERF_BENCH_CSV', 'TABPERF_BENCH_THREADS', 'DX_RAPIER_COUNTERS') { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }
      if ($currentEngines -contains $t) {
        $env:TABPERF_ENGINE = $t; $env:TABPERF_RENDERER = $(if ($Renderer) { $Renderer } elseif ($Dxvk) { 'dxvk' } else { 'd3d9' })
        if ($Diag) {
          $env:TABPERF_BENCH = '1'; $env:TABPERF_BENCH_CSV = Join-Path $OutDir "$t-diag-$r.csv"; $env:TABPERF_BENCH_THREADS = "$Threads"
          if ($Counters) { $env:DX_RAPIER_COUNTERS = '1' }
        }
      }
      Write-Host "[$t run $r/$Runs] $(Get-Date -Format 'HH:mm:ss') starting the game (until $Zombies zombies or $Timeout s)..."
      $p = Start-Process -FilePath $exe -WorkingDirectory $GameDir -PassThru
      if (-not $p.WaitForExit(($Timeout + 600) * 1000)) {
        Write-Warning "[$t run $r] no exit - stopping the game"
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; $p.WaitForExit(30000) | Out-Null
      }
      Start-Sleep -Seconds 3   # let the game release its files before the next swap
      if (-not (Test-Path $csv)) { Write-Warning "[$t run $r] no results ($csv) - see %APPDATA%\TABPerf\tabbench.log"; continue }
      $s = Summarize $csv $t $r
      if (-not $s) { Write-Warning "[$t run $r] the CSV is empty"; continue }
      $results += $s
      Write-Host ("[$t run $r] {0}: {1} zombies after {2} s (game {3} s); at 90k+: speed {4}, frame {5} ms (logic wait {6} ms)" -f $s.end, $s.zombies_max, $s.real_s, $s.game_s, $s.speed_90k, $s.frame_ms_90k, $s.logic_wait_ms_90k)
    }
  }
}
finally {
  foreach ($v in $vars) { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }
  Restore-Game
  if ((Hash $gameDll) -ne $gameDllHash) { Write-Warning "The game folder's DXPhysicsNative.dll differs from before the runs - check $gameDll" }
  if ($createdAppId) { Remove-Item $appId -ErrorAction SilentlyContinue }
}

if (-not $results.Count) { throw 'No run produced results (see %APPDATA%\TABPerf\tabbench.log)' }
$summary = Join-Path $OutDir 'summary.csv'
$results | Export-Csv $summary -NoTypeInformation -Encoding UTF8
''
$results | Format-Table target, renderer, run, end, zombies_max, real_s, game_s, speed_90k, frame_ms_90k, logic_wait_ms_90k, cpu_cores_90k -AutoSize | Out-String | Write-Host
Write-Host "Results: $OutDir (summary.csv, <target>-<run>.csv). Charts: python scripts\charts.py --game $OutDir"
