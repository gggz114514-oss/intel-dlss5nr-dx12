# Developer / historical private-bridge workflow.
# Current DX12 user installer: repository root Install-NR.cmd.
# See docs/DX12-DEVELOPMENT.md before using this legacy build path.
param(
    [string]$GameDirectory = '',
    [string]$FullDll = '',
    [string]$SmallDll = '',
    [string]$FullPackage = '',
    [string]$SmallPackage = '',
    [string]$Glslang = '',
    [string]$VulkanInclude = '',
    [string]$OptiScalerSource = '',
    [string]$SevenZip = '7z',
    [string]$Python = 'python',
    [string]$CMake = 'cmake',
    [switch]$DownloadOnly
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
function Checked([scriptblock]$Command) {
    & $Command
    if ($LASTEXITCODE) { throw "Command failed ($LASTEXITCODE). Game installation stopped." }
}
if (Get-Process Cyberpunk2077 -ErrorAction SilentlyContinue) { throw 'Exit Cyberpunk 2077 first.' }
if (!$GameDirectory -and !$DownloadOnly) { $GameDirectory = Read-Host 'Cyberpunk 2077 directory (or bin\x64)' }
if (!$DownloadOnly) {
    $gameBin = $GameDirectory
    if (Test-Path -LiteralPath (Join-Path $gameBin 'bin/x64/Cyberpunk2077.exe')) { $gameBin = Join-Path $gameBin 'bin/x64' }
    if (!(Test-Path -LiteralPath (Join-Path $gameBin 'Cyberpunk2077.exe'))) { throw 'Cyberpunk2077.exe not found.' }
}
$cache = Join-Path $root 'local/installer'
New-Item -ItemType Directory -Force $cache | Out-Null
if (!$FullPackage -or !$SmallPackage) {
    # Toolchains are explicit prerequisites; never change a driver or install a system SDK silently.
    if (!$Glslang -and $env:VULKAN_SDK) { $Glslang = Join-Path $env:VULKAN_SDK 'Bin/glslangValidator.exe' }
    if (!$VulkanInclude -and $env:VULKAN_SDK) { $VulkanInclude = Join-Path $env:VULKAN_SDK 'Include' }
    if (!$DownloadOnly) {
        if (!(Get-Command $CMake -ErrorAction SilentlyContinue)) { throw 'Install CMake 3.24+ or pass -CMake.' }
        if (!$Glslang -or !(Test-Path -LiteralPath $Glslang)) { throw 'Pass -Glslang pointing to glslang 16.5.0.' }
        if (!$VulkanInclude -or !(Test-Path -LiteralPath (Join-Path $VulkanInclude 'vulkan/vulkan.h'))) { throw 'Pass -VulkanInclude or set VULKAN_SDK.' }
    }
    $modelArgs = @('--out', (Join-Path $cache 'models'), '--seven-zip', $SevenZip)
    if ($FullDll) { $modelArgs += @('--full', $FullDll) }
    if ($SmallDll) { $modelArgs += @('--small', $SmallDll) }
    Checked { & $Python (Join-Path $PSScriptRoot 'fetch_install_models.py') @modelArgs }
    if ($DownloadOnly) { return }
    $models = Get-Content (Join-Path $cache 'models/models.json') -Raw | ConvertFrom-Json
    if (!$OptiScalerSource) {
        $OptiScalerSource = Join-Path $cache 'OptiScaler'
        if (!(Test-Path -LiteralPath $OptiScalerSource)) {
            Checked { git clone https://github.com/optiscaler/OptiScaler.git $OptiScalerSource }
        }
        Checked { git -C $OptiScalerSource checkout 7534ad00bf9e590eedb99e8dd9fd8c89dae3654f }
    }
    $venv = Join-Path $cache 'venv'
    if (!(Test-Path -LiteralPath "$venv/Scripts/python.exe")) { Checked { & $Python -m venv $venv } }
    $buildPython = "$venv/Scripts/python.exe"
    Checked { & $buildPython -m pip install -r (Join-Path $root 'requirements-model.txt') }
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $fullModel = Join-Path $cache "$stamp/full-model"
    $smallModel = Join-Path $cache "$stamp/small-model"
    $FullPackage = Join-Path $cache "$stamp/full"
    $SmallPackage = Join-Path $cache "$stamp/small"
    & "$PSScriptRoot/Build.ps1" -Profile vk102 -Dll $models.full -OptiScalerSource $OptiScalerSource -Glslang $Glslang -VulkanInclude $VulkanInclude -Python $buildPython -CMake $CMake -Output $fullModel -PackageOutput $FullPackage
    & "$PSScriptRoot/Build.ps1" -Profile small -Dll $models.small -FullModel $fullModel -OptiScalerSource $OptiScalerSource -Glslang $Glslang -VulkanInclude $VulkanInclude -Python $buildPython -CMake $CMake -Output $smallModel -PackageOutput $SmallPackage
}
Checked { & $Python "$PSScriptRoot/install_cyberpunk.py" --game $gameBin --full $FullPackage --small $SmallPackage --cache $cache --seven-zip $SevenZip --install-optiscaler }
