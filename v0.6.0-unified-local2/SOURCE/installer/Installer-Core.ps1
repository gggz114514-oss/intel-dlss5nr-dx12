Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
Import-Module "$PSHOME/Modules/Microsoft.PowerShell.Utility" -Force

function Read-JsonFile([string]$Path){Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json}
function Get-ChildPath([string]$Root,[string]$Relative){
 $rootPath=[IO.Path]::GetFullPath($Root).TrimEnd('\')
 $path=[IO.Path]::GetFullPath((Join-Path $rootPath $Relative))
 if(!$path.StartsWith($rootPath+'\',[StringComparison]::OrdinalIgnoreCase)){throw "路径超出所选目录 / Path escapes selected directory: $Relative"}
 return $path
}
function Get-Bundle([string]$Root){
 $bundle=Read-JsonFile (Join-Path $Root 'BUNDLE.json')
 $engine=Get-ChildPath $Root 'Engine.ps1'
 if((Get-FileHash -LiteralPath $engine).Hash -ne $bundle.engine_sha256){throw '安装引擎校验失败 / Installer engine checksum mismatch.'}
 return $bundle
}
function Get-GameStatus([string]$ExeName){
 switch($ExeName.ToLowerInvariant()){
  'yysls.exe' {'本机实测：燕云国服，超分、NR、DLSS-G → XeFG。 / Locally tested: Where Winds Meet CN, SR, NR and DLSS-G to XeFG.'}
  'cyberpunk2077.exe' {'本机实测：赛博朋克 2077，超分、NR、DLSS-G → XeFG。 / Locally tested: Cyberpunk 2077, SR, NR and DLSS-G to XeFG.'}
  'witcher3.exe' {'本机实测：巫师三 DX12，超分、NR、DLSS-G → XeFG。 / Locally tested: The Witcher 3 DX12, SR, NR and DLSS-G to XeFG.'}
  default {'所选游戏未经本机实测，可以继续安装。异环、鸣潮已有用户反馈成功。 / This game has not been tested locally; installation is allowed. Users report success with Neverness to Everness and Wuthering Waves.'}
 }
}
function Get-OptProxies([string]$GameRoot,$Bundle){
 $hashes=@($Bundle.variants.New.host_sha256,$Bundle.variants.Legacy.host_sha256)+@($Bundle.previous_host_sha256)
 foreach($file in @(Get-ChildItem -LiteralPath $GameRoot -File -Filter '*.dll')){
  $info=$file.VersionInfo
  if(($info.ProductName+' '+$info.FileDescription) -match 'OptiScaler'){$file.FullName;continue}
  if($file.Name -in @('dxgi.dll','winhttp.dll','version.dll','nvngx.dll','OptiScaler.dll')){
   if((Get-FileHash -LiteralPath $file.FullName).Hash -in $hashes){$file.FullName}
  }
 }
}
function Get-InstallPlan([string]$GameExe,[string]$BundleRoot){
 $exe=[IO.Path]::GetFullPath($GameExe)
 if([IO.Path]::GetExtension($exe) -ine '.exe' -or !(Test-Path -LiteralPath $exe -PathType Leaf)){throw '请选择实际游戏 EXE / Select the actual game EXE.'}
 if([IO.Path]::GetFileName($exe) -ieq 'witcher3.exe' -and (Split-Path (Split-Path $exe -Parent) -Leaf) -eq 'x64'){
  $dx12=Join-Path (Split-Path (Split-Path $exe -Parent) -Parent) 'x64_dx12/witcher3.exe'
  if(Test-Path -LiteralPath $dx12 -PathType Leaf){$exe=[IO.Path]::GetFullPath($dx12)}
 }
 $root=Split-Path -Parent $exe;$bundle=Get-Bundle $BundleRoot
 $record=Join-Path $root 'intel-nr-install.json';$receipt=$null;$backup=$null
 if(Test-Path -LiteralPath $record){
  $receipt=Read-JsonFile $record;$backup=[IO.Path]::GetFullPath($receipt.backup)
  foreach($f in $receipt.files){$null=Get-ChildPath $root $f.path}
  $needed=@($receipt.files | Where-Object {$_.existed})
  if($needed.Count -gt 0 -and !(Test-Path -LiteralPath $backup -PathType Container)){
   $relocated=Get-ChildPath $root ('intel-nr-backups/'+(Split-Path $backup -Leaf))
   if(Test-Path -LiteralPath $relocated -PathType Container){$backup=$relocated}
  }
  foreach($f in $needed){
   $original=Get-ChildPath $backup $f.path
   if(!(Test-Path -LiteralPath $original -PathType Leaf)){throw "旧版恢复文件缺失，尚未修改游戏 / Old restore file missing; game unchanged: $original"}
  }
 }
 $proxies=@(Get-OptProxies $root $bundle)
 $residuals=@('OptiScaler.ini','nr-intel','nr_hdr_proxy.hlsl','nrvk_bridge.dll')
 $residuals+=@(Get-ChildItem -LiteralPath $root -File | Where-Object {$_.Name -match '^(OptiScaler.*\.(log|bak.*)|AmdSpoofing\.log|amd_bridge\.log)$'} | ForEach-Object {$_.Name})
 $plugins=Join-Path $root 'plugins'
 if(Test-Path -LiteralPath $plugins -PathType Container){
  # Earlier project installs used a private NR ASI plus Python/runtime folders.
  # Archive those project components; retain unrelated user plugins.
  $oldNrNames=@('nr-bundle','nr-runtime','nr-route-control','nrbc-comparison-v1','nr_bridge.ini','nr_backend_int8.py','nr_backend_mixed_direct720.py','nr_backend_mixed.py','nr_backend.json','nr_cache_only.py','nr_components.py','nr_direct_meter_v2.py','nr_hdr_proxy.hlsl','nr_local_paths.json','nr_local_paths.py','nr_mixed_n2_scope.py','nr_numeric_cost_meter_v1.py','nr_prepare.py','nr_renderer_canonical.hlsl','nr-cost-meter-entry-diag.txt','nr-cost-meter.enable','mixed_direct720.py','g0-game-choice-v1')
  $residuals+=@(Get-ChildItem -LiteralPath $plugins | Where-Object {$_.Name -match '^(CyberpunkNRBridge|cyberpunk_nr_|NRB-deferred-identity|nr-runtime\.junction-bak-|_nr_pkg_backup_|_fp16_candidate_backup_)' -or $_.Name -in $oldNrNames} | ForEach-Object {'plugins/'+$_.Name})
 }
 $residuals=@($residuals | Select-Object -Unique | Where-Object {Test-Path -LiteralPath (Get-ChildPath $root $_)})
 $hasOld=$null -ne $receipt -or $proxies.Count -gt 0 -or $residuals.Count -gt 0
 [pscustomobject]@{GameExe=$exe;GameRoot=$root;ExeName=[IO.Path]::GetFileName($exe);HasOld=$hasOld;Receipt=$receipt;RecordPath=$record;RestoreBackup=$backup;Proxies=$proxies;Residuals=$residuals;Status=(Get-GameStatus ([IO.Path]::GetFileName($exe)));ManagedFiles=$(if($receipt){@($receipt.files).Count}else{0})}
}
function Get-ConfirmationText($Plan,[string]$Action,[string]$Variant){
 $verb=if($Action -eq 'Uninstall'){'卸载 / Uninstall'}else{'安装 / Install'}
 $text="$verb`r`n$($Plan.GameExe)`r`n`r`n$($Plan.Status)`r`n"
 if($Action -ne 'Uninstall'){
  $choice=if($Variant -eq 'New'){'新版 v0.5（推荐） / New v0.5 (recommended)'}else{'旧版 v0.4（回退） / Legacy v0.4 (rollback)'}
  $text+="`r`n所选方案 / Selected package: $choice`r`n"
 }
 if($Plan.HasOld){
  $text+="`r`n即将卸载旧 OPT，移除其已记录文件并恢复可恢复的原文件。旧配置、已识别的 OPT 入口和残留 NR 文件将归档；安装会使用全新默认配置。`r`nOld OPT will be uninstalled: recorded files are removed and available originals restored. Old settings, identified OPT entry DLLs and remaining NR files are archived; installation uses fresh defaults.`r`n已记录文件 / Recorded files: $($Plan.ManagedFiles); 识别入口 / Identified entry DLLs: $($Plan.Proxies.Count)`r`n"
  $targets=@($Plan.Proxies | ForEach-Object {[IO.Path]::GetFileName($_)})+@($Plan.Residuals)
  $text+="归档目标示例 / Archive targets (sample): $((@($targets | Select-Object -First 6)) -join ', ')`r`n"
 }
 if(!$Plan.Receipt -and $Plan.HasOld){$text+="`r`n未找到旧安装记录：只归档已识别的 OPT 入口、配置及 NR 文件。无法证明来源的游戏 DLL 不会删除；本安装器替换它们前会备份。`r`nNo old receipt: only identified OPT entries, settings and NR files are archived. Unverified game DLLs are retained and backed up before replacement.`r`n"}
 $text+="`r`n其他用户插件、存档及游戏文件不会按名称批量清空。备份保留在游戏目录的 intel-nr-backups。`r`nOther user plugins, saves and game files are not bulk-deleted. Backups remain under intel-nr-backups in the game directory.`r`n`r`n继续？ / Continue?"
 return $text
}
function Stage-Variant([string]$BundleRoot,[string]$Variant){
 $bundle=Get-Bundle $BundleRoot;$spec=$bundle.variants.$Variant
 $cache=Join-Path $env:LOCALAPPDATA ('IntelNR/UnifiedPackages/'+$spec.id)
 New-Item -ItemType Directory -Force -Path $cache | Out-Null
 foreach($f in $spec.files){
  $blob=Get-ChildPath $BundleRoot ('content/'+$f.sha256)
  if((Get-FileHash -LiteralPath $blob).Hash -ne $f.sha256){throw "安装包文件校验失败 / Package checksum mismatch: $($f.path)"}
  $dest=Get-ChildPath $cache $f.path;New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
  Copy-Item -LiteralPath $blob -Destination $dest -Force
 }
 [IO.File]::WriteAllText((Join-Path $cache 'PACKAGE-MANIFEST.json'),($spec.manifest | ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false)))
 return $cache
}
function Invoke-Engine([string]$BundleRoot,[string]$GameExe,[string]$Package,[string]$Log,[switch]$Uninstall){
 $args=@('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',(Join-Path $BundleRoot 'Engine.ps1'),'-GameDir',$GameExe)
 if($Uninstall){$args+='-Uninstall'}else{$args+=@('-PackageDir',$Package)}
 $ps=Join-Path $env:WINDIR 'System32/WindowsPowerShell/v1.0/powershell.exe'
 & $ps @args *> $Log
 if($LASTEXITCODE -ne 0){throw "操作失败，详见日志 / Operation failed; see log: $Log"}
}
function Invoke-InstallPlan($Plan,[string]$BundleRoot,[ValidateSet('Install','Uninstall')][string]$Action,[ValidateSet('New','Legacy')][string]$Variant='New',[switch]$RemovalConfirmed){
 if(!$RemovalConfirmed){throw '尚未确认操作，游戏未修改 / Operation not confirmed; game unchanged.'}
 if(Get-Process ([IO.Path]::GetFileNameWithoutExtension($Plan.GameExe)) -ErrorAction SilentlyContinue){throw '请先退出游戏 / Exit the game first.'}
 # Complete payload verification before removing the existing installation.
 $package=if($Action -eq 'Install'){Stage-Variant $BundleRoot $Variant}else{$null}
 if($Action -eq 'Uninstall' -and !$Plan.HasOld){return [pscustomobject]@{Result='NothingInstalled';Message='未检测到 OPT，无需卸载 / No OPT installation detected.'}}
 $archive=Get-ChildPath $Plan.GameRoot ('intel-nr-backups/unified-'+(Get-Date -Format yyyyMMdd-HHmmss-fff))
 New-Item -ItemType Directory -Path $archive | Out-Null
 $snapshot=Join-Path $archive 'before';$moved=New-Object 'System.Collections.Generic.List[object]'
 $saved=New-Object 'System.Collections.Generic.List[string]'
 $capture=@($Plan.Proxies | ForEach-Object {$_.Substring($Plan.GameRoot.Length+1)})
 if($Plan.Receipt){$capture+=@($Plan.Receipt.files | ForEach-Object {$_.path});$capture+='intel-nr-install.json'}
 $capture+='OptiScaler.ini'
 foreach($rel in @($capture | Select-Object -Unique)){
  $src=Get-ChildPath $Plan.GameRoot $rel
  if(Test-Path -LiteralPath $src -PathType Leaf){$dest=Get-ChildPath $snapshot $rel;New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null;Copy-Item -LiteralPath $src -Destination $dest;$saved.Add($rel)}
 }
 try{
  if($Plan.Receipt){
   $Plan.Receipt.backup=$Plan.RestoreBackup
   [IO.File]::WriteAllText($Plan.RecordPath,($Plan.Receipt | ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false)))
   Invoke-Engine $BundleRoot $Plan.GameExe '' (Join-Path $archive 'uninstall.log') -Uninstall
  }
  $bundle=Get-Bundle $BundleRoot
  $remaining=@(Get-OptProxies $Plan.GameRoot $bundle | ForEach-Object {$_.Substring($Plan.GameRoot.Length+1)})+@($Plan.Residuals)
  foreach($rel in @($remaining | Select-Object -Unique)){
   $src=Get-ChildPath $Plan.GameRoot $rel
   if(!(Test-Path -LiteralPath $src)){continue}
   $dest=Get-ChildPath (Join-Path $archive 'removed') $rel;New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
   # Same-volume entry move; junction targets are never traversed.
   if((Get-Item -LiteralPath $src).PSIsContainer){[IO.Directory]::Move($src,$dest)}else{Move-Item -LiteralPath $src -Destination $dest}
   $moved.Add([pscustomobject]@{path=$rel;archive=$dest})
  }
  if($Action -eq 'Install'){Invoke-Engine $BundleRoot $Plan.GameExe $package (Join-Path $archive 'install.log')}
 }catch{
  # The engine rolls back its own failed copy. Restore the previous OPT state too.
  foreach($item in $moved){
   $dest=Get-ChildPath $Plan.GameRoot $item.path
   if(!(Test-Path -LiteralPath $dest)){
    New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
    if((Get-Item -LiteralPath $item.archive).PSIsContainer){[IO.Directory]::Move($item.archive,$dest)}else{Move-Item -LiteralPath $item.archive -Destination $dest}
   }
  }
  foreach($rel in $saved){$dest=Get-ChildPath $Plan.GameRoot $rel;New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null;Copy-Item -LiteralPath (Get-ChildPath $snapshot $rel) -Destination $dest -Force}
  throw
 }
 [pscustomobject]@{Result='Success';Action=$Action;Variant=$Variant;Backup=$archive;Message='操作完成，备份已保留 / Completed; backups retained.'}
}
