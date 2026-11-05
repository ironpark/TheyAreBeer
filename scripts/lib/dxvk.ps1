# DXVK release used by the mod (dot-source: . "$PSScriptRoot\lib\dxvk.ps1"): https://github.com/doitsujin/dxvk, zlib license.
# Get-DxvkD3d9 <cache folder> downloads the release once, checks its SHA-256 and keeps the 64-bit d3d9.dll and the
# license text (from the repository, the archive has none) there; returns the d3d9.dll path.

$DxvkVersion = '3.1.1'
$DxvkSha256 = '40565b4a724aadc4433fa4e010b4b23916d9b1f1baeee64e17186db94f54e608'

function Get-DxvkD3d9([string]$dir) {
  $dll = Join-Path $dir 'd3d9.dll'; $lic = Join-Path $dir 'LICENSE'; $stamp = Join-Path $dir 'version.txt'
  if ((Test-Path $dll) -and (Test-Path $lic) -and (Test-Path $stamp) -and ((Get-Content $stamp -Raw).Trim() -eq $DxvkVersion)) { return $dll }
  New-Item -ItemType Directory -Force $dir | Out-Null
  $tgz = Join-Path $dir "dxvk-$DxvkVersion.tar.gz"
  Write-Host "DXVK: downloading $DxvkVersion"
  [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
  $ProgressPreference = 'SilentlyContinue'
  Invoke-WebRequest -UseBasicParsing "https://github.com/doitsujin/dxvk/releases/download/v$DxvkVersion/dxvk-$DxvkVersion.tar.gz" -OutFile $tgz
  $sha = (Get-FileHash $tgz -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($sha -ne $DxvkSha256) { Remove-Item $tgz; throw "DXVK download: SHA-256 $sha, expected $DxvkSha256" }
  $tmp = Join-Path $dir 'extract'; if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }; New-Item -ItemType Directory $tmp | Out-Null
  # Windows' own tar (bsdtar): a GNU tar earlier in PATH (Git) reads "C:\..." as a remote host
  & (Join-Path $env:WINDIR 'System32\tar.exe') -xzf $tgz -C $tmp "dxvk-$DxvkVersion/x64/d3d9.dll"
  if ($LASTEXITCODE -ne 0) { throw 'DXVK: could not extract x64/d3d9.dll' }
  Copy-Item (Join-Path $tmp "dxvk-$DxvkVersion\x64\d3d9.dll") $dll -Force
  # the release archive has no license text: the one of the same tag in the repository
  Invoke-WebRequest -UseBasicParsing "https://raw.githubusercontent.com/doitsujin/dxvk/v$DxvkVersion/LICENSE" -OutFile $lic
  Remove-Item $tmp -Recurse -Force; Remove-Item $tgz
  Set-Content $stamp $DxvkVersion
  return $dll
}
