# Run the mod's automatic in-game benchmark for one or more physics engines and summarize the results.
#   powershell -ExecutionPolicy Bypass -File scripts\bench.ps1 [-Engine chipmunk,rapier,original] [-Runs 1] [-Seconds 120]
#     [-Factor 4] [-Delay 5] [-Seed 20261104] [-Threads -1] [-Zoom max|off] [-OutDir <folder>] [-GameDir <folder>]
#
# Each run starts the game with the TABPERF_* environment variables below (they win over engine.json), so engine.json is
# left alone. The mod then starts a fixed survival game (seed -Seed), zooms out as far as the game allows (-Zoom max),
# turns damage off and triggers the final swarm -Factor times after -Delay seconds, writes one CSV row per second
# for -Seconds seconds, and quits the game.
#   TABPERF_BENCH=1 TABPERF_BENCH_AUTO=1 TABPERF_ENGINE TABPERF_BENCH_SECONDS TABPERF_BENCH_DELAY TABPERF_BENCH_FACTOR
#   TABPERF_BENCH_SEED TABPERF_BENCH_THREADS TABPERF_BENCH_ZOOM TABPERF_BENCH_CSV
# Results: <OutDir>\<engine>-<run>.csv (one per run) and summary.csv; default OutDir bench-out\<date-time> in the repo.
#
# Needs the mod installed (scripts\install.ps1) and Steam running. The game normally restarts itself through Steam,
# which would drop the environment variables; a steam_appid.txt in the game folder makes it run directly. The script
# creates that file for the runs and removes it afterwards (a steam_appid.txt that was already there is kept).
param(
  [string[]]$Engine = @('chipmunk', 'rapier'),
  [int]$Runs = 1,
  [int]$Seconds = 120,
  [int]$Factor = 4,
  [int]$Delay = 5,
  [int]$Seed = 20261104,
  [int]$Threads = -1,
  [ValidateSet('max', 'off')][string]$Zoom = 'max',
  [string]$OutDir = '',
  [string]$GameDir = ''
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\game.ps1')

# -File passes "-Engine a,b" as one string: split it here
$Engine = @($Engine | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim().ToLowerInvariant() } | Where-Object { $_ })
$known = 'chipmunk', 'rapier', 'original'
foreach ($e in $Engine) { if ($known -notcontains $e) { throw "Unknown -Engine $e (use $($known -join ', '))" } }
if ($Runs -lt 1) { throw '-Runs must be 1 or more' }
if ($Seconds -lt 30) { throw '-Seconds must be 30 or more (the mod runs at least 30 s)' }

$GameDir = Resolve-GameDir $GameDir
$exe = Join-Path $GameDir $TabExe
$mod = Join-Path $GameDir 'Mods\TABPerf'
if (-not (Test-Path (Join-Path $mod 'TABPerf.dll'))) { throw "TABPerf is not installed in $mod - run scripts\install.ps1 first" }
foreach ($e in $Engine) {
  if ($e -ne 'original' -and -not (Test-Path (Join-Path $mod "$e\DXPhysicsNative.dll"))) { throw "Engine $e is not installed in $mod\$e - run scripts\build.ps1 -Target $e, then scripts\install.ps1" }
}
if (Get-Process ($TabExe -replace '\.exe$', '') -ErrorAction SilentlyContinue) { throw 'The game is already running - close it first.' }
if (-not (Get-Process steam -ErrorAction SilentlyContinue)) { Write-Warning 'Steam does not seem to be running; the game needs it to start.' }

$repo = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) { $OutDir = Join-Path $repo ('bench-out\' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

$vars = 'TABPERF_BENCH', 'TABPERF_BENCH_AUTO', 'TABPERF_ENGINE', 'TABPERF_BENCH_SECONDS', 'TABPERF_BENCH_DELAY', 'TABPERF_BENCH_FACTOR',
        'TABPERF_BENCH_SEED', 'TABPERF_BENCH_THREADS', 'TABPERF_BENCH_ZOOM', 'TABPERF_BENCH_CSV'
# Loading the level takes a while: allow -Delay + -Seconds plus 5 minutes before giving up on a run.
$timeout = $Delay + $Seconds + 300

# CSV rows of one run -> summary numbers (seconds with physics steps, not paused, no marker note).
function Summarize($csv, $engineName, $run) {
  $rows = @(Import-Csv $csv | Where-Object { $_.note -eq '' -and $_.paused -eq '0' -and [int]$_.phys_steps -gt 0 })
  if (-not $rows.Count) { return $null }
  $inv = [Globalization.CultureInfo]::InvariantCulture
  function Avg($set, $col) { $v = @($set | ForEach-Object { [double]::Parse($_.$col, $inv) }); if ($v.Count) { ($v | Measure-Object -Average).Average } else { 0 } }
  $last = @($rows | Select-Object -Last 30)   # the heaviest part: the crowd only grows (damage is off)
  [pscustomobject]@{
    engine = $engineName; run = $run; dll = $rows[0].engine; seconds = $rows.Count
    zombies_max = ($rows | ForEach-Object { [int]$_.zombies } | Measure-Object -Maximum).Maximum
    step_ms = [math]::Round((Avg $rows 'step_avg_ms'), 2); frame_ms = [math]::Round((Avg $rows 'frame_avg_ms'), 2)
    last30_zombies = [math]::Round((Avg $last 'zombies'), 0)
    last30_step_ms = [math]::Round((Avg $last 'step_avg_ms'), 2); last30_frame_ms = [math]::Round((Avg $last 'frame_avg_ms'), 2)
    last30_steps_per_s = [math]::Round((Avg $last 'phys_steps'), 1)
  }
}

$createdAppId = $false
$appId = Join-Path $GameDir 'steam_appid.txt'
$results = @()
try {
  if (-not (Test-Path $appId)) { [IO.File]::WriteAllText($appId, "$TabSteamAppId"); $createdAppId = $true }
  $env:TABPERF_BENCH = '1'; $env:TABPERF_BENCH_AUTO = '1'
  $env:TABPERF_BENCH_SECONDS = "$Seconds"; $env:TABPERF_BENCH_DELAY = "$Delay"; $env:TABPERF_BENCH_FACTOR = "$Factor"
  $env:TABPERF_BENCH_SEED = "$Seed"; $env:TABPERF_BENCH_THREADS = "$Threads"; $env:TABPERF_BENCH_ZOOM = $Zoom
  for ($r = 1; $r -le $Runs; $r++) {
    foreach ($e in $Engine) {
      $csv = Join-Path $OutDir "$e-$r.csv"
      Remove-Item $csv -ErrorAction SilentlyContinue
      $env:TABPERF_ENGINE = $e; $env:TABPERF_BENCH_CSV = $csv
      Write-Host "[$e run $r/$Runs] starting the game (about $($Delay + $Seconds) s after the level loads)..."
      $p = Start-Process -FilePath $exe -WorkingDirectory $GameDir -PassThru
      if (-not $p.WaitForExit($timeout * 1000)) {
        Write-Warning "[$e run $r] no exit after $timeout s - stopping the game"
        Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
      }
      if (-not (Test-Path $csv)) { Write-Warning "[$e run $r] no results ($csv) - see %APPDATA%\TABPerf\billions.log"; continue }
      $s = Summarize $csv $e $r
      if (-not $s) { Write-Warning "[$e run $r] the CSV has no measured seconds"; continue }
      $results += $s
      Write-Host ("[$e run $r] {0}: up to {1} zombies, step {2} ms, last 30 s: {3} zombies, step {4} ms, frame {5} ms" -f $s.dll, $s.zombies_max, $s.step_ms, $s.last30_zombies, $s.last30_step_ms, $s.last30_frame_ms)
    }
  }
}
finally {
  foreach ($v in $vars) { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }
  if ($createdAppId) { Remove-Item $appId -ErrorAction SilentlyContinue }
}

if (-not $results.Count) { throw "No run produced results (see %APPDATA%\TABPerf\billions.log)" }
$summary = Join-Path $OutDir 'summary.csv'
$results | Export-Csv $summary -NoTypeInformation -Encoding UTF8
''
$results | Format-Table engine, run, zombies_max, step_ms, frame_ms, last30_zombies, last30_step_ms, last30_frame_ms, last30_steps_per_s -AutoSize | Out-String | Write-Host
Write-Host "Results: $OutDir (summary.csv, <engine>-<run>.csv)"
