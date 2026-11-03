[CmdletBinding()]
param([string]$GameDir, [string]$PackageDir, [switch]$Uninstall, [switch]$CollectLogs)
$ErrorActionPreference='Stop'
# Use inbox Windows PowerShell modules even when launched from PowerShell 7.
Import-Module "$PSHOME/Modules/Microsoft.PowerShell.Utility" -Force
Import-Module "$PSHOME/Modules/Microsoft.PowerShell.Archive" -Force
try {
 if (!$PackageDir) {
  $spec=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'PACKAGE.json') -Raw | ConvertFrom-Json
  $cache=Join-Path $env:LOCALAPPDATA ('IntelNR/Packages/'+$spec.version)
  $zip=Join-Path $cache $spec.filename
  $PackageDir=Join-Path $cache $spec.directory
  New-Item -ItemType Directory -Force -Path $cache | Out-Null
  if (!(Test-Path -LiteralPath $zip)) {
   [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
   Write-Host 'Downloading the complete Intel NR DX12 package...'
   $part=$zip+'.download'
   Invoke-WebRequest -Uri $spec.url -OutFile $part -UseBasicParsing
   if ((Get-FileHash -LiteralPath $part -Algorithm SHA256).Hash -ne $spec.sha256) { throw 'Downloaded ZIP failed SHA256 verification.' }
   Move-Item -LiteralPath $part -Destination $zip
  }
  if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne $spec.sha256) { throw 'Cached ZIP failed SHA256 verification; remove that ZIP and download again.' }
  # Re-extract from the verified ZIP, including the installer itself.
  Expand-Archive -LiteralPath $zip -DestinationPath $cache -Force
 }
 $argsMap=@{PackageDir=$PackageDir}
 if ($GameDir) {$argsMap.GameDir=$GameDir}
 if ($Uninstall) {$argsMap.Uninstall=$true}
 if ($CollectLogs) {$argsMap.CollectLogs=$true}
 & (Join-Path $PackageDir 'Setup-Intel.ps1') @argsMap
 if ($LASTEXITCODE) {exit $LASTEXITCODE}
} catch { Write-Host $_.Exception.Message -ForegroundColor Red; exit 1 }
