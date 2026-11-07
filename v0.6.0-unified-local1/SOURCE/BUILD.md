# DX12 通用版源码与构建

普通用户下载 [完整安装包](../README.zh-CN.md)，无需以下开发环境。本页用于恢复项目、修改宿主或重新构建。

## 源码分层

| 位置 | 内容 |
|---|---|
| `source/vulkan` | Full / Small Vulkan 组件、池分配、输入输出和设备生命周期 |
| `source/network-fp16-20261027`、`source/network-small-20261028` | 已采纳网络与 Small 变体 |
| `source/optiscaler-overlay` | 本版 OPT 宿主修改和新增 Intel 适配；GPL-3.0 |
| `tools/intel-nr` | 通用安装器、下载启动器、打包器、宿主覆盖工具 |
| `source/bridge` | 历史赛博朋克私有桥，当前安装器不部署该 ASI |

## 取得固定上游与应用 Intel 修改

不要把覆盖文件应用到任意新版 OPT。覆盖工具会校验上游 commit、每个原文件和目标文件的 SHA256，已有未知修改则停止。

```powershell
git clone https://github.com/TheAutomatic/dlss-5-amd-project.git local/opt-host
git -C local/opt-host checkout dd542eb28006a27cba37ba86c37dbd022bdba9a5
git -C local/opt-host submodule update --init --recursive
python tools/intel-nr/apply-host-overlay.py --upstream local/opt-host
```

`UPSTREAM.json` 列出每一个覆盖文件及原始身份，其余文件由上述固定上游恢复。完整上游 GPL、NOTICE 和来源链在覆盖目录中保留。

## 编译宿主

使用 Windows SDK 10.0.26100.0、MSVC 14.44.35207、x64 开发命令行。还需完成上游工程的依赖准备（子模块、库文件），按照固定上游的构建文档处理；本仓库不复制第三方开发 SDK。

在上游目录执行：

```powershell
msbuild OptiScaler-DLSSNR-PreSR-Multipass-main/OptiScaler/OptiScaler.vcxproj /m /t:Build /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /p:VCToolsVersion=14.44.35207 /p:WindowsTargetPlatformVersion=10.0.26100.0 /p:PostBuildEventUseInBuild=false
```

配套 shader 是 `OptiScaler/dlssnr/backend/intel/nr_input_adapter.hlsl`，打包时命名为 `nr_hdr_proxy.hlsl`，必须与宿主同版。身份生成头由工程生成，不从开发机复制。

## Full / Small Vulkan 组件

需要 Python 3.12+／NumPy、CMake 3.24+、glslang 16.5.0、Vulkan headers 和 MSVC。模型提取按 [ASSETS](../ASSETS.md)；Full 先生成，Small 通过 `--full-model` 复用已验证的共享标量。

```powershell
python -m pip install -r requirements-model.txt
python tools/extract_model.py --profile vk102 --dll C:/Models/Full/nvngx_dlssnr.dll --out local/vk102
python tools/build_assets.py --model local/vk102 --glslang C:/Tools/glslang/bin/glslang.exe
python tools/extract_model.py --profile small --full-model local/vk102 --dll C:/Models/Small/nvngx_dlssnr.dll --out local/small
python tools/build_assets.py --model local/small --glslang C:/Tools/glslang/bin/glslang.exe
cmake -S source/vulkan -B build/vulkan-small -A x64 -DNR_MODEL_DIR=C:/Dev/intel-dlss5nr-dx12/local/small -DVULKAN_INCLUDE_DIR=C:/VulkanSDK/Include
cmake --build build/vulkan-small --config Release
ctest --test-dir build/vulkan-small -C Release --output-on-failure
```

把模型目录改为 `local/vk102` 并使用独立 build 目录即可构建 Full。路径按自己的环境调整。旧 `tools/Build.ps1` 还会构建历史私有桥，只用于开发恢复；不要把它生成的 ASI 当作新 OPT 通用宿主。

## 重打完整安装包

已有完整包时，直接保留其模型／第三方依赖并替换匹配的宿主和 shader：

```powershell
python tools/intel-nr/build-oneclick.py --base-package C:/Packages/Intel-NR-DX12-20261103 --host C:/Build/OptiScaler.dll --shader C:/Build/nr_input_adapter.hlsl --sr-xmx C:/Components/amd_fidelityfx_upscaler_dx12.dll --dlss-model C:/Components/nvngx_dlss.dll --spoof-plugin C:/Components/AmdSpoofing.asi --component-notices C:/Components/notices --version v0.5.0-dx12 --out local/my-dx12-package
```

该工具自动生成逐文件清单及 Install／Uninstall／Collect-Logs 入口。需要更新 Vulkan DLL 时，在私有 staging 的相应 `payload/nr-intel/full` 或 `small` 中放置匹配 DLL，再运行打包工具生成清单，不能手改清单绕过哈希。

## 发布检查

```powershell
python tools/tests/test_intel_oneclick.py
python tools/check_publication.py --out local/publication.json
```

检查完整 ZIP 的所有 payload 哈希和许可证，使用模拟游戏目录验安装／卸载；生产改动另按影响范围做数值与真实游戏验证。覆盖文件身份以 UPSTREAM.json 为准。

2026-11-05：燕云国服 yysls.exe 已实测 DLSS/FSR XMX、Full/Small NR、游戏 DLSS-G 输入转 XeFG。修复包含旧版游戏 CRT 下的 NR mutex 初始化、完成后仍保留的录制准入、游戏 Streamline 目录挂钩、国服游戏名匹配和显式帧号对齐。模型资源和 input shader 不变，Vulkan DLL 因 mutex 兼容选项重编。发布宿主移除了临时异常捕获。

安装器专项、完整 payload 哈希、完整模拟目录安装/卸载按本版记录。完整 tests/host/run.cmd ci 的原有 nr_install_status 退出问题未被这轮专项替代，不能称全 CI 通过。真实场景验收见 docs/releases/20261105-yysls-dlssg-acceptance.json；重新安装后的验收单独记录。
