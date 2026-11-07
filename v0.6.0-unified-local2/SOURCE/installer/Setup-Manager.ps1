[CmdletBinding()]
param(
 [string]$GameExe,
 [ValidateSet('Inspect','Install','Uninstall')][string]$Action='Inspect',
 [ValidateSet('New','Legacy')][string]$Variant='New',
 [switch]$RemovalConfirmed,
 [string]$BundleRoot,
 [string]$PreviewPath
)
$ErrorActionPreference='Stop'
if(!$BundleRoot){$BundleRoot=$PSScriptRoot}
. (Join-Path $PSScriptRoot 'Installer-Core.ps1')
if($GameExe){
 try{
  $plan=Get-InstallPlan $GameExe $BundleRoot
  if($Action -eq 'Inspect'){
   [pscustomobject]@{GameExe=$plan.GameExe;OldOptDetected=$plan.HasOld;ManagedFiles=$plan.ManagedFiles;Proxies=$plan.Proxies;Residuals=$plan.Residuals;Status=$plan.Status;Confirmation=(Get-ConfirmationText $plan 'Install' $Variant)} | ConvertTo-Json -Depth 5
  }else{
   if(!$RemovalConfirmed){
    Write-Host (Get-ConfirmationText $plan $Action $Variant)
    $answer=Read-Host '输入 YES 确认，其他输入取消 / Type YES to confirm; anything else cancels'
    if($answer -cne 'YES'){Write-Host '已取消，游戏未修改 / Cancelled; game unchanged.';exit 0}
   }
   Invoke-InstallPlan $plan $BundleRoot $Action $Variant -RemovalConfirmed | ConvertTo-Json -Depth 5
  }
 }catch{Write-Error $_;exit 1}
 exit 0
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
$form=New-Object Windows.Forms.Form
$form.Text='Intel NR — 安装与卸载 / Install & Uninstall'
$form.ClientSize=New-Object Drawing.Size(944,752)
$form.MinimumSize=New-Object Drawing.Size(960,791)
$form.MaximumSize=$form.MinimumSize
$form.StartPosition='CenterScreen'
$form.Font=New-Object Drawing.Font('Microsoft YaHei UI',10)
$form.BackColor=[Drawing.Color]::FromArgb(247,249,252)
$script:busy=$false
function Label([string]$Text,[int]$X,[int]$Y,[int]$W,[int]$H){
 $c=New-Object Windows.Forms.Label;$c.Text=$Text;$c.Location=New-Object Drawing.Point($X,$Y);$c.Size=New-Object Drawing.Size($W,$H);$form.Controls.Add($c);return $c
}
function Button([string]$Text,[int]$X,[int]$Y,[int]$W,[int]$H){
 $c=New-Object Windows.Forms.Button;$c.Text=$Text;$c.Location=New-Object Drawing.Point($X,$Y);$c.Size=New-Object Drawing.Size($W,$H);$form.Controls.Add($c);return $c
}
$title=Label 'Intel NR 统一安装器 / Unified Installer' 24 18 896 40
$title.Font=New-Object Drawing.Font('Microsoft YaHei UI',17,[Drawing.FontStyle]::Bold)
$null=Label "新版本机实测通过：燕云国服、赛博朋克 2077、巫师三 DX12。`r`nNew version tested locally: Where Winds Meet CN, Cyberpunk 2077, The Witcher 3 DX12.`r`n用户反馈成功：异环、鸣潮。 / User-reported success: Neverness to Everness, Wuthering Waves." 24 70 896 62
$null=Label '实际游戏 EXE（选择渲染程序，不是启动器） / Actual game EXE (renderer, not launcher)' 24 140 896 28
$pathBox=New-Object Windows.Forms.TextBox;$pathBox.Location=New-Object Drawing.Point(24,176);$pathBox.Size=New-Object Drawing.Size(736,30);$form.Controls.Add($pathBox)
$browse=Button '浏览 / Browse…' 776 172 144 36
$compat=Label "任意游戏 EXE 均可选择，没有游戏准入限制。`r`nAny game EXE may be selected; there is no game allowlist.`r`n选择后在此显示该游戏的测试状态。 / The selected game's test status appears here." 24 224 896 94
$compat.ForeColor=[Drawing.Color]::FromArgb(48,68,93)
$null=Label '完整版本 / Complete package version' 24 330 896 28
$versionBox=New-Object Windows.Forms.ComboBox;$versionBox.DropDownStyle='DropDownList';$versionBox.Location=New-Object Drawing.Point(24,366);$versionBox.Size=New-Object Drawing.Size(896,32)
$null=$versionBox.Items.Add('新版 v0.5 — 推荐，含 NR 与 Intel 兼容修复 / New v0.5 — Recommended')
$null=$versionBox.Items.Add('旧版 v0.4 — 完整旧宿主与运行库，用于回退 / Legacy v0.4 — Full rollback package')
$versionBox.SelectedIndex=0;$form.Controls.Add($versionBox)
$null=Label "新版包含已测游戏的兼容性修复，兼容性更好，优先推荐；新版无异常就使用新版。其他游戏仍需实测。`r`nThe new version improves compatibility in tested games and is recommended. Keep it if it works; other games still need testing.`r`n旧版不只是另一条补帧通道：它保留旧宿主、运行库及配套组件。 / Legacy includes the old host, runtimes and components, not just another FG route." 24 414 896 94
$install=Button '检查并安装 / Inspect & Install' 24 526 432 50
$install.BackColor=[Drawing.Color]::FromArgb(213,232,252)
$uninstall=Button '检查并卸载 / Inspect & Uninstall' 480 526 440 50
$inspect=Button '只检查，不修改 / Inspect only — no changes' 24 592 896 38
$status=Label '就绪。选择游戏后会显示测试状态。 / Ready. Select a game to see its test status.' 24 650 896 46
$null=Label 'Windows 10/11 x64 · DX12 · 无需预装 OPT / No prior OPT required · 备份保留 / Backups retained' 24 712 896 30

function Inspect-Selection{
 if(!$pathBox.Text){throw '请先选择游戏 EXE / Select a game EXE first.'}
 $plan=Get-InstallPlan $pathBox.Text $BundleRoot
 $compat.Text=$plan.Status
 $status.Text=if($plan.HasOld){"检测到旧 OPT：记录文件 $($plan.ManagedFiles)，入口 $($plan.Proxies.Count)。删除前还会再次确认。`r`nOld OPT detected: $($plan.ManagedFiles) recorded files, $($plan.Proxies.Count) entry DLL(s). Confirmation is required before removal."}else{'未检测到旧 OPT。 / No old OPT detected.'}
 return $plan
}
$browse.Add_Click({
 $picker=New-Object Windows.Forms.OpenFileDialog;$picker.Title='选择实际游戏 EXE / Select actual game EXE';$picker.Filter='游戏程序 / Game executable (*.exe)|*.exe'
 if($picker.ShowDialog() -eq 'OK'){
  $pathBox.Text=$picker.FileName
  try{$null=Inspect-Selection}catch{[Windows.Forms.MessageBox]::Show($_.Exception.Message,'检查失败 / Inspection failed','OK','Error') | Out-Null}
 }
 $picker.Dispose()
})
$inspect.Add_Click({try{$null=Inspect-Selection}catch{[Windows.Forms.MessageBox]::Show($_.Exception.Message,'检查失败 / Inspection failed','OK','Error') | Out-Null}})
function Run-Selection([string]$SelectedAction){
 try{
  $plan=Inspect-Selection
  if($SelectedAction -eq 'Uninstall' -and !$plan.HasOld){[Windows.Forms.MessageBox]::Show('未检测到 OPT，无需卸载 / No OPT detected; nothing to uninstall.','提示 / Notice','OK','Information') | Out-Null;return}
  $selectedVariant=if($versionBox.SelectedIndex -eq 0){'New'}else{'Legacy'}
  $answer=[Windows.Forms.MessageBox]::Show((Get-ConfirmationText $plan $SelectedAction $selectedVariant),'删除前确认 / Confirm before removal','YesNo','Warning','Button2')
  if($answer -ne 'Yes'){$status.Text='已取消，游戏未修改 / Cancelled; game unchanged.';return}
  $script:busy=$true
  foreach($control in @($browse,$pathBox,$versionBox,$install,$uninstall,$inspect)){$control.Enabled=$false}
  $status.Text='正在校验、备份并处理文件，请等待。 / Verifying, backing up and processing files; please wait.'
  $form.UseWaitCursor=$true;[Windows.Forms.Application]::DoEvents()
  $result=Invoke-InstallPlan $plan $BundleRoot $SelectedAction $selectedVariant -RemovalConfirmed
  $status.Text=$result.Message
  [Windows.Forms.MessageBox]::Show(($result.Message+"`r`n`r`n备份与日志 / Backups and logs:`r`n"+$result.Backup),'完成 / Complete','OK','Information') | Out-Null
 }catch{
  $status.Text='操作未完成。请查看错误及备份日志。 / Operation incomplete; check the error and backup logs.'
  [Windows.Forms.MessageBox]::Show($_.Exception.Message,'操作失败 / Operation failed','OK','Error') | Out-Null
 }finally{
  $script:busy=$false;$form.UseWaitCursor=$false
  foreach($control in @($browse,$pathBox,$versionBox,$install,$uninstall,$inspect)){$control.Enabled=$true}
 }
}
$install.Add_Click({Run-Selection 'Install'})
$uninstall.Add_Click({Run-Selection 'Uninstall'})
$form.Add_FormClosing({param($sender,$eventArgs) if($script:busy){$eventArgs.Cancel=$true}})
if($PreviewPath){
 $form.StartPosition='Manual';$form.Location=New-Object Drawing.Point(-20000,-20000)
 $form.CreateControl();$form.Show();[Windows.Forms.Application]::DoEvents()
 $bitmap=New-Object Drawing.Bitmap($form.Width,$form.Height)
 $form.DrawToBitmap($bitmap,(New-Object Drawing.Rectangle(0,0,$form.Width,$form.Height)))
 $bitmap.Save($PreviewPath,[Drawing.Imaging.ImageFormat]::Png);$bitmap.Dispose();$form.Close()
}else{[Windows.Forms.Application]::Run($form)}
$form.Dispose()
