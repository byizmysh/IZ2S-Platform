param([string]$Destination, [switch]$NoShortcut, [switch]$NoLaunch)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
$Root = Split-Path -Parent $PSScriptRoot
try {
  $Architecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString().ToLowerInvariant()
} catch {
  $Architecture = $env:PROCESSOR_ARCHITECTURE.ToLowerInvariant()
  if ($env:PROCESSOR_ARCHITEW6432) { $Architecture = $env:PROCESSOR_ARCHITEW6432.ToLowerInvariant() }
}
switch ($Architecture) { 'x64' {} 'amd64' {} default {throw 'This installer requires Windows x64. Windows ARM64 and 32-bit Windows are not supported.'} }
$Arch = 'x64'
if (-not $env:LOCALAPPDATA) { throw 'A normal Windows user profile with LOCALAPPDATA is required.' }
$Bootstrap = Join-Path $env:LOCALAPPDATA ('IZ2S Platform\bootstrap\node-' + $Arch)
$Node = Join-Path $Bootstrap 'node.exe'
if (-not (Test-Path -LiteralPath $Node)) {
  Write-Host 'Downloading a private Node.js 24 runtime and verifying its SHA-256...'
  $Temporary = Join-Path ([IO.Path]::GetTempPath()) ('iz2s-node-' + [guid]::NewGuid())
  New-Item -ItemType Directory -Path $Temporary | Out-Null
  try {
    $Base = 'https://nodejs.org/dist/latest-v24.x'
    $Checks = (Invoke-WebRequest -UseBasicParsing -Uri ($Base+'/SHASUMS256.txt') -TimeoutSec 120).Content
    $Pattern = '^([a-f0-9]{64})\s+(node-v24\.\d+\.\d+-win-' + $Arch + '\.zip)$'
    $MatchesFound = @($Checks -split "`n" | ForEach-Object {if ($_.Trim() -match $Pattern) {[pscustomobject]@{Hash=$Matches[1];File=$Matches[2]}}})
    if ($MatchesFound.Count -ne 1) { throw 'Could not resolve the official Node.js archive.' }
    $Release = $MatchesFound[0]
    $Archive = Join-Path $Temporary $Release.File
    Invoke-WebRequest -UseBasicParsing -Uri ($Base+'/'+$Release.File) -OutFile $Archive -TimeoutSec 300
    if ((Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Release.Hash) { throw 'Node.js checksum verification failed.' }
    Expand-Archive -LiteralPath $Archive -DestinationPath (Join-Path $Temporary 'unpacked')
    $Extracted = Join-Path (Join-Path $Temporary 'unpacked') ([IO.Path]::GetFileNameWithoutExtension($Release.File))
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Bootstrap) | Out-Null
    # A concurrent installer must not merge another partial runtime.
    if (Test-Path -LiteralPath $Bootstrap) { throw 'Bootstrap folder already exists but Node.js is missing. Remove that incomplete bootstrap folder and retry.' }
    Move-Item -LiteralPath $Extracted -Destination $Bootstrap
  } finally { Remove-Item -LiteralPath $Temporary -Recurse -Force -ErrorAction SilentlyContinue }
}
& $Node -e 'if(Number(process.versions.node.split(String.fromCharCode(46))[0])<24)process.exit(1)'
if ($LASTEXITCODE -ne 0) { throw 'The private Node.js runtime is damaged. Remove its bootstrap folder and retry.' }
$Arguments = @((Join-Path $Root 'distribution\install.cjs'), '--node-home', $Bootstrap)
if ($Destination) { $Arguments += @('--destination', $Destination) }
if ($NoShortcut) { $Arguments += '--no-shortcut' }
if ($NoLaunch) { $Arguments += '--no-launch' }
& $Node @Arguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
