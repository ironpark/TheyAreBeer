# Build TABPerf: the physics engines and the engine mod.
#   powershell -ExecutionPolicy Bypass -File scripts\build.ps1 [-Target all|chipmunk|harness|rapier|mod[,...]] [-RefDir <folder>] [-Csc <csc.exe>]
#
# Targets (built in this order; default all):
#   chipmunk  native\out\DXPhysicsNative.dll          multi-core Chipmunk 7.0.0 physics (MSVC x64)
#   harness   native\out\compare.exe                  runs the game's DLL and a replacement side by side and compares them
#   rapier    native\out\rapier\DXPhysicsNative.dll   Rapier 2D physics, with rapier2d_ffi.dll and rapier2d_ffi_avx2.dll
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

# ---------- rapier: DXPhysicsNative.dll + rapier2d_ffi.dll + rapier2d_ffi_avx2.dll ----------
# rapier2d_ffi.dll is built the way the official guide says (https://rapier.rs/docs/user_guides/c/getting_started_c):
#   cmake -S bindings/c -B build -DCMAKE_BUILD_TYPE=Release -DRAPIER_DIMENSION=2 -DRAPIER_ENABLE_PARALLEL=ON -DRAPIER_SIMD_LANES=8
#   cmake --build build --config Release --parallel ; cmake --install build --prefix <sdk>
# from dimforge/rapier pinned at $rapierRev. CMake's integration tests (ctest) then check the library.
# DXPhysicsNative.dll is MSVC C only, so dx_text_pad still sits at the start of .text (TABModLoader patches base + 0x13B3);
# it delay-loads rapier2d_ffi.dll (or the AVX2 build) from its own folder.
$rapierRev = '475caa3ff4646fd9a796b67d14116256f4962bc7'   # Rapier C bindings 0.36.1+c.0
$rapierToolchain = '1.99.1'                                 # bindings/c needs Rust 1.99
# CMake options; RAPIER_DIM2 / RAPIER_F32 / RAPIER_PARALLEL on the C side must match them (the guide's consumer defines).
$rapierOptions = [ordered]@{ 'RAPIER_DIMENSION:STRING' = '2'; 'RAPIER_PRECISION:STRING' = '32'; 'RAPIER_ENABLE_PARALLEL:BOOL' = 'ON'; 'RAPIER_SIMD_LANES:STRING' = '8' }

function Test-RapierTools { -not ('git', 'cmake', 'rustup' | Where-Object { -not (Get-Command $_ -ErrorAction SilentlyContinue) }) }

function Build-Rapier {
  $out = Join-Path $native 'out\rapier'; $obj = Join-Path $out 'obj'; New-Item -ItemType Directory -Force $out, $obj | Out-Null
  $srcDir = Join-Path $native 'out\rapier-src'
  function Check([string]$what) { if ($LASTEXITCODE -ne 0) { throw "$what failed" } }

  # Rapier source at the pinned commit
  if (-not (Test-Path (Join-Path $srcDir '.git'))) { & git init -q $srcDir; Check 'git init'; & git -C $srcDir remote add origin https://github.com/dimforge/rapier.git; Check 'git remote' }
  if ((& git -C $srcDir rev-parse -q --verify HEAD 2>$null) -ne $rapierRev) {
    & git -C $srcDir fetch -q --depth 1 origin $rapierRev; Check 'git fetch'
    & git -C $srcDir -c advice.detachedHead=false checkout -q --force FETCH_HEAD; Check 'git checkout'
  }
  if (-not ((& rustup toolchain list) -match [regex]::Escape($rapierToolchain))) { & rustup toolchain install $rapierToolchain --profile minimal; Check "rustup install $rapierToolchain" }
  $env:RUSTUP_TOOLCHAIN = $rapierToolchain

  # The official CMake build of the C bindings, twice with the same options; only the Rust code generation differs:
  #   rapier2d_ffi.dll       baseline x86-64 (SSE2): runs on any x64 CPU
  #   rapier2d_ffi_avx2.dll  +avx2,+fma: the 8-lane solver runs on 256-bit registers; DXPhysicsNative.dll loads it only
  #                          when CPUID/XGETBV report AVX2 + FMA with OS support.
  $defs = ($rapierOptions.Keys | ForEach-Object { '-D' + ($_ -replace ':.*$', '') + '=' + $rapierOptions[$_] }) -join ' '
  function Build-Ffi([string]$name, [string]$rustflags, [bool]$test) {
    $buildDir = Join-Path $native "out\rapier-build$name"; $sdk = Join-Path $native "out\rapier-sdk$name"
    $env:RUSTFLAGS = "-C target-feature=+crt-static$rustflags"   # static C runtime, like the /MT C side
    # CMake runs inside the MSVC environment so the tests build with the same compiler as DXPhysicsNative.dll.
    $cfg = "-S `"$srcDir\bindings\c`" -B `"$buildDir`" -G `"NMake Makefiles`" -DCMAKE_BUILD_TYPE=Release $defs -DRAPIER_SHARED=ON -DRAPIER_BUILD_EXAMPLES=OFF"
    $steps = "`"$vcvars`" >nul && cmake $cfg && cmake --build `"$buildDir`" --config Release --parallel && cmake --install `"$buildDir`" --config Release --prefix `"$sdk`""
    if ($test) { $steps += " && ctest --test-dir `"$buildDir`" -C Release --output-on-failure" }
    $log = Join-Path $out "rapier-build$name.log"
    Invoke-Cmd $steps $log "Rapier CMake build$name or tests" -Tail
    $cache = Get-Content (Join-Path $buildDir 'CMakeCache.txt')
    foreach ($k in $rapierOptions.Keys) { if (-not ($cache -contains "$k=$($rapierOptions[$k])")) { throw "CMake cache$name lacks $k=$($rapierOptions[$k])" } }
    $tests = if ($test) { (Select-String -Path $log -Pattern 'tests passed' | Select-Object -Last 1).Line } else { 'tests skipped: this CPU has no AVX2' }
    @{ sdk = $sdk; tests = $tests }
  }
  Add-Type -Namespace W -Name K -MemberDefinition '[DllImport("kernel32.dll")] public static extern bool IsProcessorFeaturePresent(int f);'
  $base = Build-Ffi '' '' $true
  $avx2 = Build-Ffi '-avx2' ',+avx2,+fma' ([W.K]::IsProcessorFeaturePresent(40))   # PF_AVX2_INSTRUCTIONS_AVAILABLE: the AVX2 tests can run here

  # DXPhysicsNative.dll (MSVC C)
  $dll = Join-Path $out 'DXPhysicsNative.dll'; $map = Join-Path $out 'DXPhysicsNative.map'; $rdll = Join-Path $out 'rapier2d_ffi.dll'; $rdll2 = Join-Path $out 'rapier2d_ffi_avx2.dll'
  Remove-Item $dll, $map -ErrorAction SilentlyContinue
  Copy-Item (Join-Path $base.sdk 'bin\rapier2d_ffi.dll') $rdll -Force
  Copy-Item (Join-Path $avx2.sdk 'bin\rapier2d_ffi.dll') $rdll2 -Force
  $implib = Join-Path $base.sdk 'lib\rapier2d_ffi.dll.lib'; $include = Join-Path $base.sdk 'include'
  $src = '"rapier\DXPhysicsRapier.c"', '"dxphysics\dxdiag.c"', '"dxphysics\dxpool.c"'
  $flags = '/nologo /utf-8 /O2 /MT /LD /W3 /DNDEBUG /DWIN32 /DRAPIER_DIM2 /DRAPIER_F32 /DRAPIER_PARALLEL /D_CRT_SECURE_NO_WARNINGS /wd4244 /wd4305 /d1trimfile:' + $native + '\'
  Invoke-Cmd "cd /d `"$native`" && `"$vcvars`" >nul && cl $flags /I`"$include`" /Idxphysics /Fo`"$obj\\`" /Fe`"$dll`" $($src -join ' ') /link /NOLOGO /MAP:`"$map`" `"$implib`" delayimp.lib /DELAYLOAD:rapier2d_ffi.dll" (Join-Path $out 'build.log') 'Rapier DXPhysicsNative.dll build'
  Write-Host ("OK: $dll  (dx_text_pad at RVA 0x{0:X})" -f (Assert-TextPad $dll $map))
  Write-Host "OK: $rdll  (official CMake build: $(($rapierOptions.Keys | ForEach-Object { ($_ -replace ':.*$', '') + '=' + $rapierOptions[$_] }) -join ', '); $($base.tests))"
  Write-Host "OK: $rdll2  (same, Rust +avx2,+fma; $($avx2.tests))"
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
