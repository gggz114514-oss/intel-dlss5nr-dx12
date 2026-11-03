# Developer / historical private-bridge workflow.
# Current DX12 user installer: repository root Install-NR.cmd.
# See docs/DX12-DEVELOPMENT.md before using this legacy build path.
param(
    [Parameter(Mandatory=$true)][string]$Dll,
    [Parameter(Mandatory=$true)][string]$OptiScalerSource,
    [Parameter(Mandatory=$true)][string]$Glslang,
    [Parameter(Mandatory=$true)][string]$VulkanInclude,
    [string]$Python = "python",
    [string]$CMake = "cmake",
    [ValidateSet("small", "vk102")][string]$Profile = "vk102",
    [string]$Output = "",
    [string]$PackageOutput = "",
    [string]$FullModel = ""
)
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $repoRoot
try {
    if (!(Test-Path -LiteralPath (Join-Path $VulkanInclude "vulkan/vulkan.h"))) {
        throw "-VulkanInclude must name the include directory containing vulkan/vulkan.h"
    }
    if (!$Output) { $Output = "local/$Profile" }
    if (!$PackageOutput) { $PackageOutput = if ($Profile -eq "small") { "local/game-package" } else { "local/game-package-vk102" } }
    $modelPath = if ([IO.Path]::IsPathRooted($Output)) { [IO.Path]::GetFullPath($Output) } else { [IO.Path]::GetFullPath((Join-Path $repoRoot $Output)) }
    $vulkanBuildPath = Join-Path $repoRoot "build/vulkan-$Profile"
    $bridgeBuildPath = Join-Path $repoRoot "build/bridge"
    $modelArgs = @()
    if ($Profile -eq "small") {
        if (!$FullModel) { throw "Optimized Small requires -FullModel pointing to the optimized vk102 model output (for its shared blend scalar). Build vk102 first." }
        $modelArgs = @("--full-model", [IO.Path]::GetFullPath($FullModel))
    }
    & $Python -B tools/extract_model.py --profile $Profile --dll $Dll --out $modelPath @modelArgs
    if ($LASTEXITCODE) { throw "Model extraction failed" }
    & $Python -B tools/build_assets.py --model $modelPath --glslang $Glslang
    if ($LASTEXITCODE) { throw "Shader/plan preparation failed" }
    & $CMake -S source/vulkan -B $vulkanBuildPath -G "Visual Studio 17 2022" -A x64 "-DNR_MODEL_DIR=$modelPath" "-DVULKAN_INCLUDE_DIR=$VulkanInclude"
    if ($LASTEXITCODE) { throw "Vulkan configure failed" }
    & $CMake --build $vulkanBuildPath --config Release
    if ($LASTEXITCODE) { throw "Vulkan build failed" }
    $ctestPath = Join-Path (Split-Path -Parent (Get-Command $CMake).Source) "ctest.exe"
    & $ctestPath --test-dir $vulkanBuildPath -C Release --output-on-failure
    if ($LASTEXITCODE) { throw "Backend/component CPU tests failed" }
    & $CMake -S source/bridge -B $bridgeBuildPath -G "Visual Studio 17 2022" -A x64 "-DOPTISCALER_SOURCE=$OptiScalerSource" -DNRB_XESS_IDENTITY_TEST=ON -DNRB_DEFERRED_IDENTITY_TEST=ON -DNRB_LIVE_NR_TEST=ON -DNRB_MULTI_ROUTE_LIVE=ON -DNRB_TAIL_ORDER_PROBE=ON -DNRB_ONE_FRAME_COLOR_READBACK=OFF -DNRB_DLSS_COLOR_STATE_PROOF_V1=OFF -DNRB_BUILD_GENERIC_DLSS_PROBE=OFF
    if ($LASTEXITCODE) { throw "ASI configure failed" }
    & $CMake --build $bridgeBuildPath --config Release
    if ($LASTEXITCODE) { throw "ASI build failed" }
    & $ctestPath --test-dir $bridgeBuildPath -C Release --output-on-failure
    if ($LASTEXITCODE) { throw "Bridge CPU tests failed" }
    & $Python -B tools/test_panel.py --out "build/panel-test-$Profile" --backend $(if ($Profile -eq "small") { "small" } else { "102" })
    if ($LASTEXITCODE) { throw "Panel test failed" }
    & $Python -B tools/assemble_package.py --model $modelPath --out $PackageOutput --component (Join-Path $vulkanBuildPath "Release/nrvk_bridge.dll") --bridge (Join-Path $bridgeBuildPath "Release/CyberpunkNRBridge.asi")
    if ($LASTEXITCODE) { throw "Game package assembly failed" }
    Write-Host "Built $PackageOutput. Read INSTALL.md before copying game-files. No game files were changed."
} finally { Pop-Location }
