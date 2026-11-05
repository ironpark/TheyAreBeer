# Build TABPerf: the physics engines and the engine mod.
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1 [-Target all|chipmunk|harness|rapier|chipmunk-vulkan|mod|bench[,...]] [-RefDir <folder>] [-Csc <csc.exe>]
#
# Targets (built in this order; default all):
#   chipmunk  native\out\DXPhysicsNative.dll          multi-core Chipmunk 7.0.0 physics (MSVC x64)
#   harness   native\out\compare.exe                  runs the game's DLL and a replacement side by side and compares them
#   rapier    native\out\rapier\DXPhysicsNative.dll   Rapier 2D physics, with rapier2d_ffi.dll, rapier2d_ffi_avx2.dll and rapier2d_ffi_avx512.dll
#             (needs CMake 3.25+, git and rustup; "all" skips it with a warning when they are missing)
#   chipmunk-vulkan  native\out\chipmunk-vulkan\DXPhysicsNative.dll   Chipmunk2D (upstream, fetched) with the step on the
#             GPU (Vulkan compute); needs git ("all" skips it with a warning without git)
#   mod       mod\out\TABPerf.dll, ModInfos.json      the engine mod (Roslyn C#); needs the TABModLoader references in
#             -RefDir (default <repo>\refs, filled by scripts\setup.ps1)
#   bench     bench\out\TABBench.dll, ModInfos.json   the benchmark mod that scripts\bench.ps1 installs for its runs (same references)
# Needs Visual Studio 2019/2022 or Build Tools with "Desktop development with C++" (MSVC x64) and MSBuild.
# Then: scripts\install.ps1 puts the result into the game's Mods folder.
param([string[]]$Target = 'all', [string]$RefDir = '', [string]$Csc = '')
$ErrorActionPreference = 'Stop'
# -File passes "-Target a,b" as one string: split it here
$Target = @($Target | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim().ToLowerInvariant() } | Where-Object { $_ })
$known = 'all', 'chipmunk', 'harness', 'rapier', 'chipmunk-vulkan', 'mod', 'bench'
foreach ($t in $Target) { if ($known -notcontains $t) { throw "Unknown -Target $t (use $($known -join ', '))" } }
. (Join-Path $PSScriptRoot 'lib\toolchain.ps1')
. (Join-Path $PSScriptRoot 'lib\glslang.ps1')
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
#   rapier-island-epoch.patch     body state writes and new bodies keep the active-set epoch (RAPIER_STABLE_EPOCH), so the
#                                 solver contact graph is not rebuilt from scratch every step
#   rapier-pair-prefetch.patch    broad-phase pair events prefetch the colliders / graph data of the events a few ahead
#   rapier-solver-park.patch      parallel solver workers sleep after RAPIER_SYNC_SPIN spins instead of spinning
#   rapier-solver-stats.patch     parallel solver barrier wait / overflow and serial section diagnostics (RAPIER_SOLVER_DIAG)
#   rapier-solver-zorder.patch    solver bodies in Morton order of position, contacts sorted by body within a color
# The Rust side is built with fat LTO and one codegen unit. CMake's integration tests (ctest) then check the library.
# DXPhysicsNative.dll is MSVC C only, so dx_text_pad still sits at the start of .text (TABModLoader patches base + 0x13B3);
# it delay-loads rapier2d_ffi.dll (or the AVX2 / AVX-512 build) from its own folder.
$rapierRev = '475caa3ff4646fd9a796b67d14116256f4962bc7'   # Rapier C bindings 0.36.1+c.0
$rapierToolchain = '1.99.1'                                 # bindings/c needs Rust 1.99
$rapierBuildId = '1'                                        # bump when the rapier2d_ffi build steps below change (rebuilds reused builds)
# CMake options; RAPIER_DIM2 / RAPIER_F32 / RAPIER_PARALLEL on the C side must match them (the guide's consumer defines).
# RAPIER_TARGET: an explicit Rust target makes Cargo apply RUSTFLAGS (the CPU level below) only to the library; build
# scripts and proc macros, which run on the build machine, stay baseline (without it the AVX-512 build's build scripts
# die with STATUS_ILLEGAL_INSTRUCTION on a CPU without AVX-512, such as some CI runners).
$rapierOptions = [ordered]@{ 'RAPIER_DIMENSION:STRING' = '2'; 'RAPIER_PRECISION:STRING' = '32'; 'RAPIER_ENABLE_PARALLEL:BOOL' = 'ON'; 'RAPIER_SIMD_LANES:STRING' = '8'; 'RAPIER_FEATURES:STRING' = 'profiler'; 'RAPIER_TARGET:STRING' = 'x86_64-pc-windows-msvc' }
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

# A fetched source tree (Rapier, Chipmunk2D) is HEAD plus exactly these patches. A stamp file in .git holds their hashes;
# when the set or a patch changes, the tree is reset to HEAD and all of them are applied again.
function Set-SourcePatches([string]$dir, $patches, [string]$what) {
  $want = ($patches | ForEach-Object { "$($_.Name) $((Get-FileHash $_.FullName -Algorithm SHA256).Hash)" }) -join "`n"
  $stamp = Join-Path $dir '.git\tabperf-patches'
  if ((Test-Path $stamp) -and (Get-Content $stamp -Raw) -eq $want) { return }
  & git -C $dir checkout -q --force HEAD -- .; if ($LASTEXITCODE -ne 0) { throw "reset $what source failed" }
  & git -C $dir clean -q -fd; if ($LASTEXITCODE -ne 0) { throw "clean $what source failed" }
  foreach ($patch in $patches) { & git -C $dir apply $patch.FullName; if ($LASTEXITCODE -ne 0) { throw "git apply $($patch.Name) failed" } }
  [IO.File]::WriteAllText($stamp, $want)
}

# A git repository checked out at a pinned commit (shallow; LF line endings as in the repository, the patches are LF).
function Get-PinnedSource([string]$dir, [string]$url, [string]$rev) {
  function Check([string]$what) { if ($LASTEXITCODE -ne 0) { throw "$what failed ($url)" } }
  if (-not (Test-Path (Join-Path $dir '.git'))) { & git init -q $dir; Check 'git init'; & git -C $dir remote add origin $url; Check 'git remote' }
  & git -C $dir config core.autocrlf false; Check 'git config'
  if ((& git -C $dir rev-parse -q --verify HEAD 2>$null) -ne $rev) {
    & git -C $dir fetch -q --depth 1 origin $rev; Check 'git fetch'
    & git -C $dir -c advice.detachedHead=false checkout -q --force FETCH_HEAD; Check 'git checkout'
    Remove-Item (Join-Path $dir '.git\tabperf-patches') -ErrorAction SilentlyContinue
  }
}

function Build-Rapier {
  $out = Join-Path $native 'out\rapier'; $obj = Join-Path $out 'obj'; New-Item -ItemType Directory -Force $out, $obj | Out-Null
  $srcDir = Join-Path $native 'out\rapier-src'
  function Check([string]$what) { if ($LASTEXITCODE -ne 0) { throw "$what failed" } }
  $patches = @(Get-ChildItem (Join-Path $native 'rapier') -Filter *.patch | Sort-Object Name)
  $defs = ($rapierOptions.Keys | ForEach-Object { '-D' + ($_ -replace ':.*$', '') + '=' + $rapierOptions[$_] }) -join ' '
  Add-Type -Namespace W -Name K -MemberDefinition '[DllImport("kernel32.dll")] public static extern bool IsProcessorFeaturePresent(int f);'

  # Each rapier2d_ffi build is installed into native\out\rapier-sdk<variant> with a stamp of everything it is built
  # from (Rapier commit, toolchain, CMake options, patches, Rust flags). A build whose stamp matches is reused as is:
  # no source checkout, rustup or Cargo run (CI caches these folders).
  $patchIds = ($patches | ForEach-Object { "$($_.Name)=$((Get-FileHash $_.FullName -Algorithm SHA256).Hash)" }) -join ' '
  $variants = foreach ($v in $rapierVariants) {
    $name = $v.suffix -replace '_', '-'
    $sdk = Join-Path $native "out\rapier-sdk$name"
    $stampFile = Join-Path $sdk 'tabperf-build.txt'
    $stamp = "build $rapierBuildId; rev $rapierRev; rust $rapierToolchain; rustflags $($v.rustflags); options $defs; patches $patchIds"
    $have = if (Test-Path $stampFile) { @(Get-Content $stampFile) } else { @() }
    $fresh = $have.Count -ge 2 -and $have[0] -eq $stamp -and (Test-Path (Join-Path $sdk 'bin\rapier2d_ffi.dll')) -and (Test-Path (Join-Path $sdk 'LICENSE'))
    @{ v = $v; name = $name; sdk = $sdk; stampFile = $stampFile; stamp = $stamp; fresh = $fresh; tests = $(if ($fresh) { "$($have[1]) (reused build)" } else { '' })
       buildDir = (Join-Path $native "out\rapier-build$name"); log = (Join-Path $out "rapier-build$name.log"); dll = (Join-Path $out "rapier2d_ffi$($v.suffix).dll") }
  }
  $todo = @($variants | Where-Object { -not $_.fresh })

  if ($todo.Count) {
    # Rapier source at the pinned commit, with LF line endings as in the repository (the patches are LF)
    Get-PinnedSource $srcDir 'https://github.com/dimforge/rapier.git' $rapierRev
    Set-SourcePatches $srcDir $patches 'Rapier'
    if (-not ((& rustup toolchain list) -match [regex]::Escape($rapierToolchain))) { & rustup toolchain install $rapierToolchain --profile minimal; Check "rustup install $rapierToolchain" }
    # Cargo.lock is not in the Rapier repository (ignored), so a fresh checkout has none: resolve and download the crates
    # once here, before the side-by-side builds would each write Cargo.lock into the shared source folder
    Invoke-Cmd "set `"RUSTUP_TOOLCHAIN=$rapierToolchain`" && cargo fetch --manifest-path `"$srcDir\Cargo.toml`"" (Join-Path $out 'rapier-fetch.log') 'cargo fetch' -Tail

    # The variants build at the same time, one cmd.exe each (fat LTO with one codegen unit ends in a long single-threaded
    # link per variant; side by side these overlap). Each has its own build folder, log and RUSTFLAGS.
    $procs = foreach ($t in $todo) {
      $test = $t.v.pf -eq 0 -or [W.K]::IsProcessorFeaturePresent($t.v.pf)
      $t.test = $test
      # CMake runs inside the MSVC environment so the tests build with the same compiler as DXPhysicsNative.dll.
      $cfg = "-S `"$srcDir\bindings\c`" -B `"$($t.buildDir)`" -G `"NMake Makefiles`" -DCMAKE_BUILD_TYPE=Release $defs -DRAPIER_SHARED=ON -DRAPIER_BUILD_EXAMPLES=OFF"
      $steps = "`"$vcvars`" >nul && cmake $cfg && cmake --build `"$($t.buildDir)`" --config Release --parallel && cmake --install `"$($t.buildDir)`" --config Release --prefix `"$($t.sdk)`""
      if ($test) { $steps += " && ctest --test-dir `"$($t.buildDir)`" -C Release --output-on-failure" }
      Remove-Item $t.stampFile -ErrorAction SilentlyContinue
      # parentheses: the redirection covers the whole && chain, not only its last command
      $psi = New-Object Diagnostics.ProcessStartInfo 'cmd.exe', "/s /c `"($steps) > `"$($t.log)`" 2>&1`""
      $psi.UseShellExecute = $false
      $psi.EnvironmentVariables['RUSTUP_TOOLCHAIN'] = $rapierToolchain
      # fat LTO across all crates, one codegen unit: Cargo's release profile through its environment variables
      $psi.EnvironmentVariables['CARGO_PROFILE_RELEASE_LTO'] = 'fat'
      $psi.EnvironmentVariables['CARGO_PROFILE_RELEASE_CODEGEN_UNITS'] = '1'
      $psi.EnvironmentVariables['RUSTFLAGS'] = "-C target-feature=+crt-static$($t.v.rustflags)"   # static C runtime, like the /MT C side
      Write-Host "Rapier$($t.name): building ($($t.v.what)), log $($t.log)"
      @{ t = $t; p = [Diagnostics.Process]::Start($psi) }
    }
    $failed = @()
    foreach ($x in $procs) {
      $x.p.WaitForExit(); $t = $x.t
      if ($x.p.ExitCode -ne 0) { Write-Host "---- Rapier$($t.name) (exit $($x.p.ExitCode)) ----"; if (Test-Path $t.log) { Get-Content $t.log -Tail 30 | ForEach-Object { Write-Host $_ } }; $failed += "Rapier CMake build$($t.name) or tests failed (see $($t.log))"; continue }
      $cache = Get-Content (Join-Path $t.buildDir 'CMakeCache.txt')
      foreach ($k in $rapierOptions.Keys) { if (-not ($cache -contains "$k=$($rapierOptions[$k])")) { throw "CMake cache$($t.name) lacks $k=$($rapierOptions[$k])" } }
      $t.tests = if ($t.test) { (Select-String -Path $t.log -Pattern 'tests passed' | Select-Object -Last 1).Line } else { 'tests skipped: this CPU lacks the instructions' }
      Copy-Item (Join-Path $srcDir 'LICENSE') (Join-Path $t.sdk 'LICENSE') -Force
      Set-Content $t.stampFile @($t.stamp, $t.tests) -Encoding UTF8
    }
    if ($failed) { throw ($failed -join '; ') }
  }
  $built = @($variants | ForEach-Object { @{ sdk = $_.sdk; tests = $_.tests; dll = $_.dll; what = $_.v.what } })

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

# ---------- chipmunk-vulkan: DXPhysicsNative.dll (Chipmunk2D + Vulkan compute) ----------
# Chipmunk2D is fetched from its repository at $chipmunkRev into native\out\chipmunk2d-src (upstream source, with the
# patches in native\chipmunk-vulkan applied: chipmunk-filter-either.patch gives the CPU mode the game's filter rule).
# The step runs as Vulkan compute kernels (native\chipmunk-vulkan\cpvk.comp), compiled to SPIR-V by glslang
# (scripts\lib\glslang.ps1, build time only) and embedded in the DLL; the Vulkan headers come from Vulkan-Headers at
# $vulkanHeadersRev. vulkan-1.dll (the Vulkan loader, part of every Vulkan driver) is loaded at run time.
$chipmunkRev = '5b45034a8761518800e03dd79f33c3aad0bf7fee'        # Chipmunk2D master, 2026-01-08 (7.0.3 and later fixes)
$chipmunkUrl = 'https://codeberg.org/slembcke/Chipmunk2D.git'
$vulkanHeadersRev = 'b5c8f996196ba4aa6d8f97e52b5d3b6e70f7e4e2'   # Vulkan-Headers vulkan-sdk-1.4.341.0
$vulkanHeadersUrl = 'https://github.com/KhronosGroup/Vulkan-Headers.git'
# the kernels of cpvk.comp (K_<name>); cpvk_spv.h gets them as CVK_KERNELS(X), cpvk_gpu.c's enum and SPIR-V table
$cvkKernels = 'SHAPE_SCATTER', 'BODY_PATCH', 'INTEGRATE_POS', 'SHAPE_UPDATE', 'GRID_COUNT', 'GRID_SCATTER', 'GRID_SORT', 'SCAN',
  'NARROW_COUNT', 'NARROW_WRITE', 'DISABLE', 'COUNT', 'PRESTEP', 'INTEGRATE_VEL', 'WARM', 'SOLVE', 'APPLY', 'OUTPUT', 'WARM_SAVE',
  'COLOR', 'COLOR_SCATTER', 'CSOLVE'

function Build-ChipmunkVulkan {
  $here = Join-Path $native 'chipmunk-vulkan'
  $out = Join-Path $native 'out\chipmunk-vulkan'; $obj = Join-Path $out 'obj'; New-Item -ItemType Directory -Force $out, $obj | Out-Null
  $dll = Join-Path $out 'DXPhysicsNative.dll'; $map = Join-Path $out 'DXPhysicsNative.map'
  Remove-Item $dll, $map -ErrorAction SilentlyContinue
  $cpSrc = Join-Path $native 'out\chipmunk2d-src'; $vkSrc = Join-Path $native 'out\vulkan-headers'
  Get-PinnedSource $cpSrc $chipmunkUrl $chipmunkRev
  Set-SourcePatches $cpSrc @(Get-ChildItem $here -Filter *.patch | Sort-Object Name) 'Chipmunk2D'
  Get-PinnedSource $vkSrc $vulkanHeadersUrl $vulkanHeadersRev
  Copy-Item (Join-Path $cpSrc 'LICENSE.txt') (Join-Path $out 'Chipmunk2D-LICENSE.txt') -Force

  # SPIR-V: one array per kernel (cvk_<name>) and the list of kernels in obj\cpvk_spv.h
  $glslang = Get-Glslang (Join-Path $native 'out\glslang')
  $spvLog = Join-Path $out 'shaders.log'; Remove-Item $spvLog -ErrorAction SilentlyContinue
  $header = New-Object System.Text.StringBuilder
  [void]$header.Append("// generated by scripts\build.ps1 from native\chipmunk-vulkan\cpvk.comp (glslang $GlslangVersion)`r`n#pragma once`r`n#include <stdint.h>`r`n")
  foreach ($k in $cvkKernels) {
    $h = Join-Path $obj "cvk_$k.h"
    $ErrorActionPreference = 'Continue'
    $msg = & $glslang -V --target-env vulkan1.1 -S comp "-DK_$k" --vn "cvk_$k" -o $h (Join-Path $here 'cpvk.comp') 2>&1
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    Add-Content $spvLog "---- K_$k (exit $code)`r`n$($msg -join "`r`n")"
    if ($code -ne 0) { $msg | ForEach-Object { Write-Host $_ }; throw "glslang K_$k failed (see $spvLog)" }
    # glslang's header has its own #pragma once / include lines: keep the array only
    $text = (Get-Content $h -Raw) -replace '(?m)^\s*#pragma once\s*$', '' -replace '(?m)^\s*#include.*$', ''
    [void]$header.Append($text + "`r`n")
  }
  [void]$header.Append('#define CVK_KERNELS(X) ' + (($cvkKernels | ForEach-Object { "X($_)" }) -join ' ') + "`r`n")
  [IO.File]::WriteAllText((Join-Path $obj 'cpvk_spv.h'), $header.ToString())

  # Upstream Chipmunk2D sources (cpHastySpace.c needs pthreads: not used), the wrapper, the Vulkan backend, the shared
  # thread pool and crash diagnostics (dx_text_pad: see dxdiag.c).
  $src = @('"chipmunk-vulkan\DXPhysicsVulkan.c"', '"chipmunk-vulkan\cpvk_gpu.c"', '"dxphysics\dxdiag.c"', '"dxphysics\dxpool.c"')
  $src += Get-ChildItem (Join-Path $cpSrc 'src') -Filter *.c | Where-Object { $_.Name -ne 'cpHastySpace.c' } | ForEach-Object { '"' + $_.FullName + '"' }
  $flags = '/nologo /utf-8 /O2 /MT /LD /W3 /DNDEBUG /DWIN32 /DCP_USE_DOUBLES=0 /D_CRT_SECURE_NO_WARNINGS /wd4244 /wd4305 /wd4018 /wd4996 /d1trimfile:' + $native + '\'
  $inc = "/I`"$cpSrc\include`" /I`"$vkSrc\include`" /Idxphysics /I`"$obj`""
  # dxpad.asm: the second code pad (dx_text_pad2, see DXPhysicsVulkan.c), assembled by ml64
  $padObj = Join-Path $obj 'dxpad.obj'
  Invoke-Cmd "cd /d `"$native`" && `"$vcvars`" >nul && ml64 /nologo /c /Fo`"$padObj`" chipmunk-vulkan\dxpad.asm" (Join-Path $out 'build-pad.log') 'dxpad.asm (ml64)'
  Invoke-Cmd "cd /d `"$native`" && `"$vcvars`" >nul && cl $flags $inc /Fo`"$obj\\`" /Fe`"$dll`" $($src -join ' ') `"$padObj`" /link /NOLOGO /MAP:`"$map`"" (Join-Path $out 'build.log') 'Chipmunk2DVulkan DXPhysicsNative.dll build'
  $pad = Assert-TextPad $dll $map
  # the game also writes at base + 0x3AE30 (see dx_text_pad2 in DXPhysicsVulkan.c)
  $pad2 = Assert-MapCovers $dll $map 'dx_text_pad2' 0x3A000 0x3AE00 0x3AE80
  Write-Host ("OK: $dll  (dx_text_pad at RVA 0x{0:X}, dx_text_pad2 at 0x{1:X}; Chipmunk2D {2}, glslang {3})" -f $pad, $pad2, $chipmunkRev.Substring(0, 10), $GlslangVersion)
}

# ---------- mod: TABPerf.dll, bench: TABBench.dll ----------
# The references come from TABModLoader: TheyAreBillions.exe (the patched game executable - it contains the game and
# loader types) and DXVision.dll, 0Harmony.dll (the loader's Lib folder). .NET Framework 4.7.2+.
function Build-Mod([string]$folder = 'mod', [string]$name = 'TABPerf', [string]$define = 'PERFMOD') {
  if (-not $RefDir) { $script:RefDir = Join-Path $repo 'refs' }
  $refFiles = 'TheyAreBillions.exe', 'DXVision.dll', '0Harmony.dll'
  foreach ($f in $refFiles) { if (-not (Test-Path (Join-Path $RefDir $f))) { throw "Missing $f in $RefDir - run scripts\setup.ps1 or pass -RefDir" } }
  $compiler = if ($Csc) { if (-not (Test-Path $Csc)) { throw "-Csc not found: $Csc" }; $Csc } else { Get-Csc }
  $here = Join-Path $repo $folder; $out = Join-Path $here 'out'; New-Item -ItemType Directory -Force $out | Out-Null
  $fw = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
  $refs = @('System.dll', 'System.Core.dll', 'System.Drawing.dll', 'System.Windows.Forms.dll', 'System.Numerics.dll' | ForEach-Object { '-r:"' + (Join-Path $fw $_) + '"' })
  # The engine mod compiles against the DXVision the game runs (embedded in its executable), publicized
  # (scripts\tools\RefTool.cs; the mod's [IgnoresAccessChecksTo("DXVision")] reaches private members with it)
  $dxv = Join-Path $RefDir 'DXVision.dll'
  if ($folder -eq 'mod') {
    $obj = Join-Path $here 'out\obj'; New-Item -ItemType Directory -Force $obj | Out-Null
    $tool = Join-Path $obj 'RefTool.exe'; $toolSrc = Join-Path $PSScriptRoot 'tools\RefTool.cs'
    # rebuilt / rerun only when an input changed (a stamp of the inputs' paths, sizes and times next to the output:
    # older references swapped in, e.g. setup.ps1 -Version v1.1.6, count as a change too)
    $stampOf = { param($files) ($files | ForEach-Object { $i = Get-Item $_; "$($i.FullName)|$($i.Length)|$($i.LastWriteTimeUtc.Ticks)" }) -join "`n" }
    $fresh = { param($outFile, $stamp) (Test-Path $outFile) -and (Test-Path "$outFile.stamp") -and ((Get-Content "$outFile.stamp" -Raw) -eq $stamp) }
    $toolStamp = & $stampOf @($toolSrc)
    if (-not (& $fresh $tool $toolStamp)) {
      & $compiler -nologo -langversion:7.3 -optimize+ "-out:$tool" $toolSrc
      if ($LASTEXITCODE -ne 0) { throw 'RefTool build failed' }
      [IO.File]::WriteAllText("$tool.stamp", $toolStamp)
    }
    $dxv = Join-Path $obj 'ref\DXVision.dll'
    $exe = Join-Path $RefDir 'TheyAreBillions.exe'; $libDxv = Join-Path $RefDir 'DXVision.dll'
    $refStamp = & $stampOf @($tool, $exe, $libDxv)
    if (-not (& $fresh $dxv $refStamp)) {
      & $tool $exe $libDxv $dxv
      if ($LASTEXITCODE -ne 0) { throw 'DXVision reference (RefTool) failed' }
      [IO.File]::WriteAllText("$dxv.stamp", $refStamp)
    }
  }
  $refs += $refFiles | ForEach-Object { '-r:"' + $(if ($_ -eq 'DXVision.dll') { $dxv } else { Join-Path $RefDir $_ }) + '"' }
  $src = @(Get-ChildItem $here -Filter *.cs -Recurse | Where-Object { $_.FullName -notlike "$out\*" } | ForEach-Object { '"' + $_.FullName + '"' })
  # ModInfos.json "Version" is the one version number (the loader's auto-update compares it with the release tag): the
  # DLL gets it as ModBuildInfo.Version and as its informational version
  $version = (Get-Content (Join-Path $here 'ModInfos.json') -Raw -Encoding UTF8 | ConvertFrom-Json).Version
  if (-not $version) { throw "No Version in $folder\ModInfos.json" }
  $gen = Join-Path $out 'obj'; New-Item -ItemType Directory -Force $gen | Out-Null
  $info = Join-Path $gen 'ModBuildInfo.g.cs'
  [IO.File]::WriteAllText($info, "// generated by scripts\build.ps1 from $folder\ModInfos.json`r`n[assembly: System.Reflection.AssemblyInformationalVersion(`"$version`")]`r`ninternal static class ModBuildInfo { public const string Version = `"$version`"; }`r`n")
  $src += '"' + $info + '"'
  $cscArgs = @('-nologo', '-target:library', '-langversion:7.3', '-optimize+', '-deterministic', '-unsafe+', '-nowarn:1701,1702,0169', "-define:$define", "-out:`"$out\$name.dll`"") + $refs + $src
  & $compiler @cscArgs
  if ($LASTEXITCODE -ne 0) { throw "$name build failed" }
  Copy-Item (Join-Path $here 'ModInfos.json') $out -Force
  Write-Host "OK: $out\$name.dll ($version)"
}

# ---------- run ----------
$needMsvc = (Wanted 'chipmunk') -or (Wanted 'harness') -or (Wanted 'rapier') -or (Wanted 'chipmunk-vulkan')
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
if (Wanted 'chipmunk-vulkan') {
  if (Get-Command git -ErrorAction SilentlyContinue) { Build-ChipmunkVulkan }
  elseif ($all) { Write-Warning 'Chipmunk2DVulkan skipped: git is needed (scripts\build.ps1 -Target chipmunk-vulkan once it is installed).' }
  else { throw 'Chipmunk2DVulkan needs git.' }
}
if (Wanted 'mod') { Build-Mod }
if (Wanted 'bench') { Build-Mod 'bench' 'TABBench' 'TABBENCH' }
Write-Host 'Next: scripts\install.ps1 [-Engine chipmunk|rapier|chipmunk-vulkan]'
