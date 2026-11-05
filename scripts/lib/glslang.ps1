# glslang (https://github.com/KhronosGroup/glslang, BSD-3-Clause and others) compiles the Chipmunk2DVulkan compute
# kernels to SPIR-V at build time; it is not shipped. Dot-source: . "$PSScriptRoot\lib\glslang.ps1".
# Get-Glslang <cache folder> downloads the pinned release once, checks its SHA-256 and returns the glslang.exe path.

$GlslangVersion = '16.5.0'
$GlslangSha256 = '06b71298b750268c127f2ee7ae0ef7525e2068120c6c8a3a08b2f58ca6f325ce'   # glslang-16.5.0-windows-x86_64-release.zip

function Get-Glslang([string]$dir) {
  $exe = Join-Path $dir 'bin\glslang.exe'; $stamp = Join-Path $dir 'version.txt'
  if ((Test-Path $exe) -and (Test-Path $stamp) -and ((Get-Content $stamp -Raw).Trim() -eq $GlslangVersion)) { return $exe }
  New-Item -ItemType Directory -Force $dir | Out-Null
  $zip = Join-Path $dir "glslang-$GlslangVersion.zip"
  Write-Host "glslang: downloading $GlslangVersion"
  [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
  $ProgressPreference = 'SilentlyContinue'
  Invoke-WebRequest -UseBasicParsing "https://github.com/KhronosGroup/glslang/releases/download/$GlslangVersion/glslang-$GlslangVersion-windows-x86_64-release.zip" -OutFile $zip
  $sha = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($sha -ne $GlslangSha256) { Remove-Item $zip; throw "glslang download: SHA-256 $sha, expected $GlslangSha256" }
  # Windows' own tar (bsdtar) reads zip archives; a GNU tar earlier in PATH (Git) would not
  New-Item -ItemType Directory -Force (Join-Path $dir 'bin') | Out-Null
  & (Join-Path $env:WINDIR 'System32\tar.exe') -xf $zip -C $dir 'bin/glslang.exe'
  if ($LASTEXITCODE -ne 0 -or -not (Test-Path $exe)) { throw 'glslang: could not extract bin/glslang.exe' }
  Remove-Item $zip
  Set-Content $stamp $GlslangVersion
  return $exe
}
