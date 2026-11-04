# Build TABPerf: the physics engines and the engine mod.
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1 [-Target all|chipmunk|harness|rapier|mod[,...]] [-RefDir <folder>] [-Csc <csc.exe>]
#
# Targets (built in this order; default all):
#   chipmunk  native\out\DXPhysicsNative.dll          multi-core Chipmunk 7.0.0 physics (MSVC x64)
#   harness   native\out\compare.exe                  runs the game's DLL and a replacement side by side and compares them
#   rapier    native\out\rapier\DXPhysicsNative.dll   Rapier 2D physics, with rapier2d_ffi.dll, rapier2d_ffi_avx2.dll and rapier2d_ffi_avx512.dll
#             (needs CMake 3.25+, git and rustup; "all" skips it with a warning when they are missing)
#   mod       mod\out\TABPerf.dll, ModInfos.json      the engine mod (Roslyn C#); needs the TABModLoader references in
#             -RefDir (default <repo>\refs, filled by scripts\setup.ps1)
# Needs Visual Studio 2019/2022 or Build Tools with "Desktop development with C++" (MSVC x64) and MSBuild.
# Then: scripts\install.ps1 puts the result into the game's Mods folder.
param([string[]]$Target = 'all', [string]$RefDir = '', [string]$Csc = '')
$ErrorActionPreference = 'Stop'
# -File passes "-Target a,b" as one string: split it here
$Target = @($Target | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim().ToLowerInvariant() } | Where-Object { $_ })
$known = 'all', 'chipmunk', 'harness', 'rapier', 'mod'
foreach ($t in $Target) { if ($known -notcontains $t) { throw "Unknown -Target $t (use $($known -join ', '))" } }
. (Join-Path $PSScriptRoot 'lib\toolchain.ps1')
$repo = Split-Path -Parent $PSScriptRoot
$native = Join-Path $repo 'native'
$all = $Target -contains 'all'
function Wanted($t) { $all -or $Target -contains $t }

# ---------- chipmunk: DXPhysicsNative.dll ----------
function Build-Chipmunk {
  $out = Join-Path $native 'out'; $obj = Join-Path $out 'obj'; New-Item -ItemType Directory -Force $out, $obj | Out-Null
  # Remove the previous DLL first so a failed build never leaves a stale one behind.
  $dll = Join-Path $out 'DXPhysicsNative.dll'; $map = Join-Path $out 'DXPhysicsNative.map'
  Remove-Item $dll, $map -ErrorAction SilentlyContinue
  $src = Get-ChildItem (Join-Path $native 'chipmunk\src') -Filter *.c | Where-Object { $_.Name -ne 'cpHastySpace.c' } | ForEach-Object { '"chipmunk\src\' + $_.Name + '"' }
  $src += '"dxphysics\DXPhysicsNative.c"', '"dxphysics\dxpool.c"', '"dxphysics\dxdiag.c"'
  # /fp:precise /fp:contract- : same float operation order as the original DLL (needed for bit-identical results with the parallel switches off).
  # No /arch flag: SSE2 baseline, runs on any x64 CPU. /MT: static C runtime, no redistributable needed. /d1trimfile: keep build paths out of the DLL.
  $flags = '/nologo /utf-8 /O2 /MT /LD /W3 /fp:precise /fp:contract- /DNDEBUG /DWIN32 /DCP_USE_DOUBLES=0 /D_CRT_SECURE_NO_WARNINGS /wd4244 /wd4305 /wd4018 /d1trimfile:' + $native + '\'
  Invoke-Cmd "cd /d `"$native`" && `"$vcvars`" >nul && cl $flags /I`"chipmunk\include`" /I`"dxphysics`" /Fo`"$obj\\`" /Fe`"$dll`" $($src -join ' ') /link /NOLOGO /MAP:`"$map`"" (Join-Path $out 'build.log') 'Chipmunk build'
  Write-Host ("OK: $dll  (dx_text_pad at RVA 0x{0:X})" -f (Assert-TextPad $dll $map))
}

# ---------- harness: compare.exe ----------
function Build-Harness {
  $out = Join-Path $native 'out'; $obj = Join-Path $out 'obj'; New-Item -ItemType Directory -Force $out, $obj | Out-Null
  Invoke-Cmd "cd /d `"$native`" && `"$vcvars`" >nul && cl /nologo /utf-8 /O2 /MT /W3 /D_CRT_SECURE_NO_WARNINGS /Fo`"$obj\\`" /Fe`"$out\compare.exe`" harness\compare.c /link /NOLOGO" (Join-Path $out 'build-harness.log') 'compare.exe build'
  Write-Host "OK: $out\compare.exe  (usage: compare.exe <original DLL> <new DLL> [scene|all] [bodies] [steps])"
}

# ---------- rapier: DXPhysicsNative.dll + rapier2d_ffi.dll (+ _avx2, _avx512) ----------
# rapier2d_ffi.dll is built the way the official guide says (https://rapier.rs/docs/user_guides/c/getting_started_c):
#   cmake -S bindings/c -B build -DCMAKE_BUILD_TYPE=Release -DRAPIER_DIMENSION=2 -DRAPIER_ENABLE_PARALLEL=ON -DRAPIER_SIMD_LANES=8
#   cmake --build build --config Release --parallel ; cmake --install build --prefix <sdk>
# from dimforge/rapier pinned at $rapierRev, with the patches in native\rapier applied (C bindings only):
#   rapier-c-batch-write.patch    r2RigidBody_WriteStates: one world borrow for the whole push instead of one per body
#                                 and value
#   rapier-c-step-counters.patch  r2StepCounters: the pipeline's stage times of the last step (profiler feature;
#                                 off until r2SetCountersEnabled)
# The Rust side is built with fat LTO and one codegen unit. CMake's integration tests (ctest) then check the library.
# DXPhysicsNative.dll is MSVC C only, so dx_text_pad still sits at the start of .text (TABModLoader patches base + 0x13B3);
# it delay-loads rapier2d_ffi.dll (or the AVX2 / AVX-512 build) from its own folder.
$rapierRev = '475caa3ff4646fd9a796b67d14116256f4962bc7'   # Rapier C bindings 0.36.1+c.0
$rapierToolchain = '1.99.1'                                 # bindings/c needs Rust 1.99
# CMake options; RAPIER_DIM2 / RAPIER_F32 / RAPIER_PARALLEL on the C side must match them (the guide's consumer defines).
$rapierOptions = [ordered]@{ 'RAPIER_DIMENSION:STRING' = '2'; 'RAPIER_PRECISION:STRING' = '32'; 'RAPIER_ENABLE_PARALLEL:BOOL' = 'ON'; 'RAPIER_SIMD_LANES:STRING' = '8'; 'RAPIER_FEATURES:STRING' = 'profiler' }
# The same CMake build once per CPU level. DXPhysicsNative.dll picks the highest one the CPU and OS support (CPUID/XGETBV).
# pf = IsProcessorFeaturePresent id that decides whether the tests can run here.
#   rapier2d_ffi.dll         baseline x86-64 (SSE2): runs on any x64 CPU
#   rapier2d_ffi_avx2.dll    AVX2 + FMA
#   rapier2d_ffi_avx512.dll  AVX-512 (F, VL, BW, DQ, CD) instructions, same 8 lanes
$rapierVariants = @(
  @{ suffix = '';        rustflags = '';                                                                     pf = 0;  what = 'baseline x86-64' },
  @{ suffix = '_avx2';   rustflags = ',+avx2,+fma';                                                          pf = 40; what = 'Rust +avx2,+fma' },
  @{ suffix = '_avx512'; rustflags = ',+avx2,+fma,+avx512f,+avx512vl,+avx512bw,+avx512dq,+avx512cd'; pf = 41; what = 'Rust +avx2,+fma,+avx512f/vl/bw/dq/cd' }
)

function Test-RapierTools { -not ('git', 'cmake', 'rustup' | Where-Object { -not (Get-Command $_ -ErrorAction SilentlyContinue) }) }

# The Rapier working tree is HEAD plus exactly these patches. A stamp file in .git holds their hashes; when the set or a
# patch changes, the tree is reset to HEAD and all of them are applied again.
function Set-RapierPatches([string]$dir, $patches) {
  $want = ($patches | ForEach-Object { "$($_.Name) $((Get-FileHash $_.FullName -Algorithm SHA256).Hash)" }) -join "`n"
  $stamp = Join-Path $dir '.git\tabperf-patches'
  if ((Test-Path $stamp) -and (Get-Content $stamp -Raw) -eq $want) { return }
  & git -C $dir checkout -q --force HEAD -- .; if ($LASTEXITCODE -ne 0) { throw 'reset Rapier source failed' }
  & git -C $dir clean -q -fd; if ($LASTEXITCODE -ne 0) { throw 'clean Rapier source failed' }
  foreach ($patch in $patches) { & git -C $dir apply $patch.FullName; if ($LASTEXITCODE -ne 0) { throw "git apply $($patch.Name) failed" } }
  [IO.File]::WriteAllText($stamp, $want)
}

function Build-Rapier {
  $out = Join-Path $native 'out\rapier'; $obj = Join-Path $out 'obj'; New-Item -ItemType Directory -Force $out, $obj | Out-Null
  $srcDir = Join-Path $native 'out\rapier-src'
  function Check([string]$what) { if ($LASTEXITCODE -ne 0) { throw "$what failed" } }

  # Rapier source at the pinned commit, with LF line endings as in the repository (the patches are LF)
  if (-not (Test-Path (Join-Path $srcDir '.git'))) { & git init -q $srcDir; Check 'git init'; & git -C $srcDir remote add origin https://github.com/dimforge/rapier.git; Check 'git remote' }
  & git -C $srcDir config core.autocrlf false; Check 'git config'
  if ((& git -C $srcDir rev-parse -q --verify HEAD 2>$null) -ne $rapierRev) {
    & git -C $srcDir fetch -q --depth 1 origin $rapierRev; Check 'git fetch'
    & git -C $srcDir -c advice.detachedHead=false checkout -q --force FETCH_HEAD; Check 'git checkout'
  }
  Set-RapierPatches $srcDir @(Get-ChildItem (Join-Path $native 'rapier') -Filter *.patch | Sort-Object Name)
  if (-not ((& rustup toolchain list) -match [regex]::Escape($rapierToolchain))) { & rustup toolchain install $rapierToolchain --profile minimal; Check "rustup install $rapierToolchain" }
  $env:RUSTUP_TOOLCHAIN = $rapierToolchain
  # fat LTO across all crates, one codegen unit: Cargo's release profile through its environment variables
  $env:CARGO_PROFILE_RELEASE_LTO = 'fat'; $env:CARGO_PROFILE_RELEASE_CODEGEN_UNITS = '1'

  $defs = ($rapierOptions.Keys | ForEach-Object { '-D' + ($_ -replace ':.*$', '') + '=' + $rapierOptions[$_] }) -join ' '
  Add-Type -Namespace W -Name K -MemberDefinition '[DllImport("kernel32.dll")] public static extern bool IsProcessorFeaturePresent(int f);'
  function Build-Ffi($v) {
    $name = $v.suffix -replace '_', '-'
    $buildDir = Join-Path $native "out\rapier-build$name"; $sdk = Join-Path $native "out\rapier-sdk$name"
    $test = $v.pf -eq 0 -or [W.K]::IsProcessorFeaturePresent($v.pf)
    $env:RUSTFLAGS = "-C target-feature=+crt-static$($v.rustflags)"   # static C runtime, like the /MT C side
    # CMake runs inside the MSVC environment so the tests build with the same compiler as DXPhysicsNative.dll.
    $cfg = "-S `"$srcDir\bindings\c`" -B `"$buildDir`" -G `"NMake Makefiles`" -DCMAKE_BUILD_TYPE=Release $defs -DRAPIER_SHARED=ON -DRAPIER_BUILD_EXAMPLES=OFF"
    $steps = "`"$vcvars`" >nul && cmake $cfg && cmake --build `"$buildDir`" --config Release --parallel && cmake --install `"$buildDir`" --config Release --prefix `"$sdk`""
    if ($test) { $steps += " && ctest --test-dir `"$buildDir`" -C Release --output-on-failure" }
    $log = Join-Path $out "rapier-build$name.log"
    Invoke-Cmd $steps $log "Rapier CMake build$name or tests" -Tail
    $cache = Get-Content (Join-Path $buildDir 'CMakeCache.txt')
    foreach ($k in $rapierOptions.Keys) { if (-not ($cache -contains "$k=$($rapierOptions[$k])")) { throw "CMake cache$name lacks $k=$($rapierOptions[$k])" } }
    $tests = if ($test) { (Select-String -Path $log -Pattern 'tests passed' | Select-Object -Last 1).Line } else { 'tests skipped: this CPU lacks the instructions' }
    @{ sdk = $sdk; tests = $tests; dll = (Join-Path $out "rapier2d_ffi$($v.suffix).dll"); what = $v.what }
  }
  $built = @($rapierVariants | ForEach-Object { Build-Ffi $_ })

  # DXPhysicsNative.dll (MSVC C)
  $dll = Join-Path $out 'DXPhysicsNative.dll'; $map = Join-Path $out 'DXPhysicsNative.map'
  Remove-Item $dll, $map -ErrorAction SilentlyContinue
  foreach ($b in $built) { Copy-Item (Join-Path $b.sdk 'bin\rapier2d_ffi.dll') $b.dll -Force }
  $implib = Join-Path $built[0].sdk 'lib\rapier2d_ffi.dll.lib'; $include = Join-Path $built[0].sdk 'include'
  $src = '"rapier\DXPhysicsRapier.c"', '"dxphysics\dxdiag.c"', '"dxphysics\dxpool.c"'
  $flags = '/nologo /utf-8 /O2 /MT /LD /W3 /DNDEBUG /DWIN32 /DRAPIER_DIM2 /DRAPIER_F32 /DRAPIER_PARALLEL /D_CRT_SECURE_NO_WARNINGS /wd4244 /wd4305 /d1trimfile:' + $native + '\'
  Invoke-Cmd "cd /d `"$native`" && `"$vcvars`" >nul && cl $flags /I`"$include`" /Idxphysics /Fo`"$obj\\`" /Fe`"$dll`" $($src -join ' ') /link /NOLOGO /MAP:`"$map`" `"$implib`" delayimp.lib /DELAYLOAD:rapier2d_ffi.dll" (Join-Path $out 'build.log') 'Rapier DXPhysicsNative.dll build'
  Write-Host ("OK: $dll  (dx_text_pad at RVA 0x{0:X})" -f (Assert-TextPad $dll $map))
  $opts = ($rapierOptions.Keys | ForEach-Object { ($_ -replace ':.*$', '') + '=' + $rapierOptions[$_] }) -join ', '
  foreach ($b in $built) { Write-Host "OK: $($b.dll)  (official CMake build: $opts, fat LTO; $($b.what); $($b.tests))" }
}

# ---------- mod: TABPerf.dll ----------
# The references come from TABModLoader: TheyAreBillions.exe (the patched game executable - it contains the game and
# loader types) and DXVision.dll, 0Harmony.dll (the loader's Lib folder). .NET Framework 4.7.2+.
function Build-Mod {
  if (-not $RefDir) { $script:RefDir = Join-Path $repo 'refs' }
  $refFiles = 'TheyAreBillions.exe', 'DXVision.dll', '0Harmony.dll'
  foreach ($f in $refFiles) { if (-not (Test-Path (Join-Path $RefDir $f))) { throw "Missing $f in $RefDir - run scripts\setup.ps1 or pass -RefDir" } }
  $compiler = if ($Csc) { if (-not (Test-Path $Csc)) { throw "-Csc not found: $Csc" }; $Csc } else { Get-Csc }
  $here = Join-Path $repo 'mod'; $out = Join-Path $here 'out'; New-Item -ItemType Directory -Force $out | Out-Null
  $fw = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
  $refs = @('System.dll', 'System.Core.dll', 'System.Drawing.dll', 'System.Windows.Forms.dll', 'System.Numerics.dll' | ForEach-Object { '-r:"' + (Join-Path $fw $_) + '"' })
  $refs += $refFiles | ForEach-Object { '-r:"' + (Join-Path $RefDir $_) + '"' }
  $src = Get-ChildItem $here -Filter *.cs | ForEach-Object { '"' + $_.FullName + '"' }
  $cscArgs = @('-nologo', '-target:library', '-langversion:7.3', '-optimize+', '-deterministic', '-unsafe+', '-nowarn:1701,1702,0169', '-define:PERFMOD', "-out:`"$out\TABPerf.dll`"") + $refs + $src
  & $compiler @cscArgs
  if ($LASTEXITCODE -ne 0) { throw 'Mod build failed' }
  Copy-Item (Join-Path $here 'ModInfos.json') $out -Force
  Write-Host "OK: $out\TABPerf.dll"
}

# ---------- run ----------
$needMsvc = (Wanted 'chipmunk') -or (Wanted 'harness') -or (Wanted 'rapier')
if ($needMsvc) { Assert-NoSpaces $native; $vcvars = Get-VcVars }
if (Wanted 'chipmunk') { Build-Chipmunk }
if (Wanted 'harness') {
  # optional when building everything: the DLLs do not need it
  if ($all) { try { Build-Harness } catch { Write-Warning $_.Exception.Message } } else { Build-Harness }
}
if (Wanted 'rapier') {
  if (Test-RapierTools) { Build-Rapier }
  elseif ($all) { Write-Warning 'Rapier skipped: git, cmake and rustup are needed (scripts\build.ps1 -Target rapier once they are installed).' }
  else { throw 'Rapier needs git, cmake and rustup.' }
}
if (Wanted 'mod') { Build-Mod }
Write-Host 'Next: scripts\install.ps1 [-Engine chipmunk|rapier]'
