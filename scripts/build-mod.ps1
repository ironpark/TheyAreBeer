# Build TABPerf.dll (engine optimization mod for TABModLoader).
#   powershell -ExecutionPolicy Bypass -File build-mod.ps1 -RefDir <folder>
# <folder> must contain, from your own TABModLoader installation:
#   TheyAreBillions.exe  (the game executable patched by TABModLoader - it contains the game and loader types)
#   DXVision.dll, 0Harmony.dll  (from the TABModLoader package, Lib folder)
# Needs the Roslyn C# compiler (Visual Studio 2019/2022 or Build Tools - "MSBuild"), and .NET Framework 4.7.2+.
# Output: mod\out\TABPerf.dll  -> copy it with mod\ModInfos.json (and native DLL, optional) into <game>\Mods\TABPerf\
param([Parameter(Mandatory = $true)][string]$RefDir, [string]$Csc = '')
$ErrorActionPreference = 'Stop'
$here = Join-Path $PSScriptRoot 'mod'
foreach ($f in 'TheyAreBillions.exe', 'DXVision.dll', '0Harmony.dll') { if (-not (Test-Path (Join-Path $RefDir $f))) { throw "Missing $f in $RefDir" } }
if (-not $Csc) {
  $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
  if (Test-Path $vswhere) { $vs = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath; if ($vs) { $Csc = Join-Path $vs 'MSBuild\Current\Bin\Roslyn\csc.exe' } }
}
if (-not $Csc -or -not (Test-Path $Csc)) { throw 'Roslyn csc.exe not found - pass -Csc <path> or install Visual Studio Build Tools.' }
$fw = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319'
$out = Join-Path $here 'out'; New-Item -ItemType Directory -Force $out | Out-Null
$src = Get-ChildItem $here -Filter *.cs | ForEach-Object { '"' + $_.FullName + '"' }
$refs = @('System.dll', 'System.Core.dll', 'System.Drawing.dll', 'System.Windows.Forms.dll', 'System.Numerics.dll') | ForEach-Object { '-r:"' + (Join-Path $fw $_) + '"' }
foreach ($f in 'TheyAreBillions.exe', 'DXVision.dll', '0Harmony.dll') { $refs += ('-r:"' + (Join-Path $RefDir $f) + '"') }
$args = @('-nologo', '-target:library', '-langversion:7.3', '-optimize+', '-deterministic', '-unsafe+', '-nowarn:1701,1702,0169', '-define:PERFMOD', "-out:`"$out\TABPerf.dll`"") + $refs + $src
& $Csc @args
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
Copy-Item (Join-Path $here 'ModInfos.json') $out -Force
Write-Host "OK: $out\TABPerf.dll"
