[CmdletBinding()]
param([string]$GameDir, [string]$PackageDir, [switch]$Uninstall, [switch]$CollectLogs)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
if(!$PackageDir){$PackageDir=$PSScriptRoot}
# Use the inbox module of this PowerShell, including when launched from PowerShell 7.
Import-Module "$PSHOME/Modules/Microsoft.PowerShell.Utility" -Force
$logRoot=Join-Path $env:LOCALAPPDATA 'IntelNR/InstallerLogs'
New-Item -ItemType Directory -Force -Path $logRoot | Out-Null
$logPath=Join-Path $logRoot ('setup-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'.log')
Start-Transcript -Path $logPath | Out-Null
trap { Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red; Write-Host $_.ScriptStackTrace; Write-Host "Log: $logPath"; Stop-Transcript | Out-Null; exit 1 }
if(!$GameDir){
 Add-Type -AssemblyName System.Windows.Forms
 $picker=New-Object System.Windows.Forms.OpenFileDialog
 $picker.Title='选择游戏的实际 DX12 EXE（不要选择启动器）'
 $picker.Filter='游戏程序 (*.exe)|*.exe'
 if($picker.ShowDialog() -ne [Windows.Forms.DialogResult]::OK){Stop-Transcript | Out-Null; exit 0}
 $GameDir=$picker.FileName
 $picker.Dispose()
}
$GameDir=[IO.Path]::GetFullPath($GameDir.Trim('"'))
$exe=$null
if(Test-Path -LiteralPath $GameDir -PathType Leaf){
 if([IO.Path]::GetExtension($GameDir) -ine '.exe'){throw 'Select the actual game EXE.'}
 $exe=[IO.Path]::GetFileNameWithoutExtension($GameDir)
 $GameDir=Split-Path -Parent $GameDir
}
if(!$exe){
 if(Test-Path "$GameDir/bin/x64_dx12/witcher3.exe"){$GameDir=Join-Path $GameDir 'bin/x64_dx12'}
 elseif(Test-Path "$GameDir/bin/x64/Cyberpunk2077.exe"){$GameDir=Join-Path $GameDir 'bin/x64'}
 $existingRecord=Join-Path $GameDir 'intel-nr-install.json'
 if(Test-Path -LiteralPath $existingRecord){$exe=(Get-Content -LiteralPath $existingRecord -Raw | ConvertFrom-Json).game}
 else{
  $exe=@('witcher3','Cyberpunk2077','ds') | Where-Object {Test-Path "$GameDir/$_.exe"} | Select-Object -First 1
  if(!$exe){
   $choices=@(Get-ChildItem -LiteralPath $GameDir -Filter '*.exe' -File)
   if($choices.Count -ne 1){throw 'Multiple or no EXEs in this folder. Select the actual game EXE instead of its folder.'}
   $exe=$choices[0].BaseName
  }
 }
}
$knownGame=$exe -in @('witcher3','Cyberpunk2077','ds')
if(!$knownGame){Write-Host '通用 DX12 安装：此游戏尚未实测。请选择游戏实际渲染 EXE，并启用 DX12 和游戏内 DLSS/超分；安装成功不等于已验证兼容。'}
if($exe -eq 'witcher3' -and (Split-Path $GameDir -Leaf) -eq 'x64'){
 $dx12=Join-Path (Split-Path $GameDir) 'x64_dx12'
 if(!(Test-Path "$dx12/witcher3.exe")){throw 'Select the DX12 version: bin/x64_dx12/witcher3.exe'}
 $GameDir=$dx12
}
$record=Join-Path $GameDir 'intel-nr-install.json'
if($CollectLogs){
 $report=Join-Path $logRoot ('report-'+(Get-Date -Format yyyyMMdd-HHmmss))
 New-Item -ItemType Directory -Path $report | Out-Null
 foreach($rel in @('OptiScaler.log','OptiScaler.1.log','OptiScaler.ini','intel-nr-install.json','nr-intel/status.json','nr-intel/nr_stall.log')){
  if(Test-Path "$GameDir/$rel"){Copy-Item -LiteralPath "$GameDir/$rel" -Destination (Join-Path $report ($rel.Replace('/','_')))}
 }
 Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,AdapterRAM | ConvertTo-Json | Set-Content "$report/gpu.json"
 Copy-Item -LiteralPath $logPath -Destination "$report/setup.log"
 Compress-Archive -Path "$report/*" -DestinationPath "$report.zip"
 Write-Host "诊断日志已导出：$report.zip（不会自动上传；不含存档、截图或帧数据）"
 Stop-Transcript | Out-Null
 exit 0
}
if(Get-Process $exe -ErrorAction SilentlyContinue){throw 'Exit the game before installing or uninstalling.'}
function Target([string]$rel){
 $p=[IO.Path]::GetFullPath((Join-Path $GameDir $rel))
 if(!$p.StartsWith($GameDir.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw "Unsafe path: $rel"}
 return $p
}
if($Uninstall){
 $receipt=Get-Content -LiteralPath $record -Raw | ConvertFrom-Json
 foreach($f in $receipt.files){
  $p=Target $f.path
  if(Test-Path -LiteralPath $p){Remove-Item -LiteralPath $p -Force}
  if($f.existed){Copy-Item -LiteralPath (Join-Path $receipt.backup $f.path) -Destination $p -Force}
 }
 Remove-Item -LiteralPath $record
 Write-Host 'Uninstalled; replaced files restored. Backup and user-generated logs retained.'
 Stop-Transcript | Out-Null
 exit 0
}
if(Test-Path $record){throw 'This installation is already managed. Run Uninstall.cmd before a clean reinstall.'}
if(![Environment]::Is64BitProcess -or [Environment]::OSVersion.Version.Major -lt 10){throw 'Windows 10/11 x64 is required. Run Install.cmd with Windows PowerShell x64.'}
if(!(Test-Path "$env:WINDIR/System32/vulkan-1.dll")){throw 'Vulkan driver is missing. Install the Intel graphics driver, restart Windows, then run Install.cmd again.'}
$vc=Get-ItemProperty 'HKLM:/SOFTWARE/Microsoft/VisualStudio/14.0/VC/Runtimes/x64' -ErrorAction SilentlyContinue
if(!$vc -or $vc.Installed -ne 1 -or [version]$vc.Version.TrimStart('v') -lt [version]'14.44.35207'){
 Write-Host '缺少 Microsoft Visual C++ x64 运行库，将下载微软官方安装器。请完成安装窗口。'
 $redist=Join-Path $logRoot 'vc_redist.x64.exe'
 [Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
 Invoke-WebRequest 'https://aka.ms/vc14/vc_redist.x64.exe' -OutFile $redist -UseBasicParsing
 $sig=Get-AuthenticodeSignature $redist
 if($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation'){throw 'Microsoft runtime signature verification failed.'}
 $vcProcess=Start-Process -FilePath $redist -ArgumentList '/install','/passive','/norestart' -Wait -PassThru
 if($vcProcess.ExitCode -eq 3010){throw 'Runtime installed. Restart Windows, then run Install.cmd again.'}
 if($vcProcess.ExitCode -ne 0){throw "Runtime installation failed: $($vcProcess.ExitCode)"}
}
$payload=Join-Path $PackageDir 'payload'
$manifest=Get-Content (Join-Path $PackageDir 'PACKAGE-MANIFEST.json') -Raw | ConvertFrom-Json
# Verify the complete package before changing any game files.
foreach($f in $manifest.files){
 $p=[IO.Path]::GetFullPath((Join-Path $payload $f.path))
 if(!$p.StartsWith([IO.Path]::GetFullPath($payload).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe package path'}
 if((Get-FileHash -LiteralPath $p).Hash -ne $f.sha256){throw "Package hash mismatch: $($f.path)"}
}
$proxy=if($exe -eq 'ds'){'winhttp.dll'}else{'dxgi.dll'}
foreach($other in @('dxgi.dll','winhttp.dll','version.dll')){
 if($other -ne $proxy -and (Test-Path "$GameDir/$other")){throw "Existing proxy $other found. Remove the old mod installation first."}
}
$backup=Join-Path $GameDir ('intel-nr-backups/'+(Get-Date -Format yyyyMMdd-HHmmss))
New-Item -ItemType Directory -Path $backup -Force | Out-Null
$actions=New-Object 'System.Collections.Generic.List[object]'
function Save([string]$rel){
 $p=Target $rel
 $exists=Test-Path -LiteralPath $p
 if($exists){$b=Join-Path $backup $rel; New-Item -ItemType Directory -Path (Split-Path $b) -Force | Out-Null; Copy-Item -LiteralPath $p -Destination $b}
 $actions.Add([pscustomobject]@{path=$rel;existed=$exists})
 New-Item -ItemType Directory -Path (Split-Path $p) -Force | Out-Null
 return $p
}
try{
 foreach($f in $manifest.files){
  $rel=if($f.path -eq 'OptiScaler.dll'){$proxy}else{$f.path}
  # Existing preferences survive upgrades; defaults are for clean installs only.
  if($rel -eq 'OptiScaler.ini' -and (Test-Path "$GameDir/$rel")){continue}
  $p=Save $rel
  Copy-Item -LiteralPath (Join-Path $payload $f.path) -Destination $p -Force
  if((Get-FileHash $p).Hash -ne $f.sha256){throw "Installed hash mismatch: $rel"}
  # Death Stranding DC has no native DLSSG input; use OptiScaler's SR input.
  if($rel -eq 'OptiScaler.ini' -and ($exe -eq 'ds' -or !$knownGame)){
   $ini=[IO.File]::ReadAllText($p).Replace('FGInput=dlssg','FGInput=upscaler')
   if(!$knownGame){$ini=[regex]::Replace($ini,'(?m)(^\[FrameGen\]\r?\n)Enabled=true','$1Enabled=false')}
   [IO.File]::WriteAllText($p,$ini,(New-Object Text.UTF8Encoding($false)))
  }
 }
 $receipt=[ordered]@{version=$manifest.version;game=$exe;profile=$(if($knownGame){$exe}else{'generic-dx12'});proxy=$proxy;backup=$backup;files=@($actions.ToArray());host_sha256=(Get-FileHash "$GameDir/$proxy").Hash}
 $receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $record -Encoding UTF8
}catch{
 foreach($f in $actions){$p=Target $f.path; if(Test-Path $p){Remove-Item -LiteralPath $p -Force}; if($f.existed){Copy-Item -LiteralPath (Join-Path $backup $f.path) -Destination $p}}
 throw
}
Write-Host "Installed Intel NR Full + Small + FSR XMX into $GameDir"
Write-Host 'Launch the DX12 game, enable DLSS/upscaling in game, press Insert for OptiScaler / Intel NR.'
Write-Host 'Clean-install defaults: Full, native NR, Classic; NVIDIA DXGI/Streamline spoofing. Generic games: frame generation off until configured in menu.'
Write-Host 'Cyberpunk/Witcher: enable frame generation in game. Death Stranding: upscaler FG input. Restart after spoofing changes.'
Write-Host '安装完成。无需安装 Python、Visual Studio、Vulkan SDK 或单独安装 OPT。'
Write-Host "安装记录：$record"
Stop-Transcript | Out-Null
