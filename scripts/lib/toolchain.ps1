# Build tool helpers (dot-source: . "$PSScriptRoot\lib\toolchain.ps1").

function Get-VsWhere {
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found - install Visual Studio Build Tools with the C++ workload.' }
  $vswhere
}

# vcvars64.bat of the newest Visual Studio / Build Tools with the MSVC x64 tools.
function Get-VcVars {
  $vs = & (Get-VsWhere) -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if (-not $vs) { throw 'MSVC x64 tools not found.' }
  $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
  if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found: $vcvars" }
  $vcvars
}

# Roslyn csc.exe of the newest Visual Studio / Build Tools with MSBuild.
function Get-Csc {
  $vs = & (Get-VsWhere) -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
  $csc = if ($vs) { Join-Path $vs 'MSBuild\Current\Bin\Roslyn\csc.exe' }
  if (-not $csc -or -not (Test-Path $csc)) { throw 'Roslyn csc.exe not found - pass -Csc <path> or install Visual Studio Build Tools.' }
  $csc
}

# /d1trimfile (keeps build paths out of the DLLs) takes the source root unquoted.
function Assert-NoSpaces([string]$path) { if ($path -match ' ') { throw "Path must not contain spaces (needed for /d1trimfile): $path" } }

# Runs a cmd.exe command line with its output in $log; on failure shows the error lines (or the log's tail) and throws.
function Invoke-Cmd([string]$cmd, [string]$log, [string]$what, [switch]$Tail) {
  $ErrorActionPreference = 'Continue'   # native tools write to stderr; only the exit code decides
  cmd /c $cmd *> $log
  if ($LASTEXITCODE -ne 0) {
    $lines = if ($Tail) { Get-Content $log -Tail 30 } else { Get-Content $log | Select-String -Pattern 'error' | Select-Object -First 20 }
    $lines | ForEach-Object { Write-Host $_ }
    throw "$what failed (see $log)"
  }
}

# TABModLoader overwrites 5 bytes at DLL base + 0x13B3; that range must lie inside dx_text_pad (8 KB of nops, dxdiag.c).
# Checks the link map, deletes the DLL when the pad is misplaced, and returns the pad RVA.
function Assert-TextPad([string]$dll, [string]$map) {
  $base = $null; $pad = $null
  foreach ($line in Get-Content $map) {
    if ($line -match 'Preferred load address is ([0-9A-Fa-f]+)') { $base = [Convert]::ToUInt64($Matches[1], 16) }
    elseif ($line -match '^\s*[0-9A-Fa-f]{4}:[0-9A-Fa-f]{8}\s+dx_text_pad\s+([0-9A-Fa-f]+)\s') { $pad = [Convert]::ToUInt64($Matches[1], 16) }
  }
  if ($null -eq $base -or $null -eq $pad) { Remove-Item $dll; throw "dx_text_pad not found in $map" }
  $padRva = $pad - $base
  if ($padRva -gt 0x13B3 -or ($padRva + 8192) -lt 0x13B8) { Remove-Item $dll; throw ('dx_text_pad at RVA 0x{0:X} does not cover the loader patch at 0x13B3' -f $padRva) }
  $padRva
}
