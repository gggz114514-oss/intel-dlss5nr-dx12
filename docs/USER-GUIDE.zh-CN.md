# 从下载到赛博朋克实机：完整使用指南

## 推荐：2026-11-01新版安装入口

[一键构建安装教程](ONE-CLICK-INSTALL.zh-CN.md)：准备开发依赖后运行根目录 `Install-Cyberpunk.cmd`，自动下载模型、构建Full和Small、备份并安装。默认 **Full + 原生·自动**；720/540/480/360p为可选降采样档，不是原生上限。流式默认开启，FSR XMX单独部署。以下保留手动构建与选择启动后端的步骤；手动示例中的Small启动选择不代表新版安装器默认值。

当前主分支提供优化 Full、优化 Small、跨帧流式桥接和后端控制面板。安装完成后可以在面板选择 Full / Small，并调整经典、自然、电影等外观参数。

Full 是 232 派发、约 17.45 ms 的版本；Small 是 108 派发、约 9.98 ms 的版本。默认流式桥接已启用。上述网络时间对应 B580、1280×720 NR 输入的记录。

下载稳定的源码快照 [v0.3.0-multires](https://github.com/gggz114514-oss/intel-dlss5nr-cyberpunk2077/releases/tag/v0.3.0-multires)，或使用 main 获取后续修订。旧 v0.2.0 是历史版本。

## 1. 下载与目录准备

下载入口：[GitHub 仓库](https://github.com/gggz114514-oss/intel-dlss5nr-cyberpunk2077)。可使用 Code → Download ZIP，或：

```powershell
git clone https://github.com/gggz114514-oss/intel-dlss5nr-cyberpunk2077.git C:\Dev\intel-dlss5nr-cyberpunk2077
cd C:\Dev\intel-dlss5nr-cyberpunk2077
```

仓库发布源码，游戏包在本机生成。建议目录如下：

```text
C:\Dev\intel-dlss5nr-cyberpunk2077  仓库与本地构建输出
C:\Dev\OptiScaler                 固定版本的构建依赖
C:\Models\Full\nvngx_dlssnr.dll   完整模型 DLL
C:\Models\Small\nvngx_dlssnr.dll  Small 模型 DLL
```

模型 DLL 当作数据读取，提取器不执行它。Full 使用 310.8.0.0 / WEIGHTS_HT；Small 使用 310.9.16.0 / WEIGHTS_CC_SMALL_FP8。精确 SHA256 见 [首页模型表](../README.zh-CN.md)。两个 DLL 使用各自独立目录。

## 2. 准备构建工具和游戏组件

构建工具：Python 3.12+、NumPy、Visual Studio 2022 C++ Build Tools、Windows SDK、CMake 3.24+、glslang 16.5.0、Vulkan headers。具体版本与依赖路径见 [INSTALL.md](../INSTALL.md)。

OptiScaler 构建依赖使用固定提交：

```powershell
git clone https://github.com/optiscaler/OptiScaler.git C:\Dev\OptiScaler
git -C C:\Dev\OptiScaler checkout 7534ad00bf9e590eedb99e8dd9fd8c89dae3654f
```

游戏端先准备可运行的 OptiScaler + FSR XMX + XeSS FG 组合。本仓库负责 NR；FSR XMX 负责超分，XeSS FG 负责帧生成。第三方运行 DLL 按其项目说明单独安装。

## 3. 先构建 Full

在仓库根目录执行，按实际安装位置修改工具路径：

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-model.txt
$py = ".\.venv\Scripts\python.exe"
$opti = "C:\Dev\OptiScaler"
$glslang = "C:\Tools\glslang\bin\glslang.exe"
$vk = "C:\VulkanSDK\Include"

.\tools\Build.ps1 -Profile vk102 -Dll "C:\Models\Full\nvngx_dlssnr.dll" `
  -OptiScalerSource $opti -Glslang $glslang -VulkanInclude $vk -Python $py
```

脚本依次完成模型提取、权重布局、shader 编译和身份检查、原生组件构建、CPU 检查以及游戏包装配。结果：

- `local/vk102/`：Full 模型构建输出。
- `local/game-package-vk102/`：Full 游戏包。

只使用 Full 时，到这里即可进入安装步骤。

## 4. 再构建 Small

Small 使用自己的模型权重和偏移专用核，同时从 Full 构建输出读取当前已采用的末端共享 blend scalar，因此先完成上一步。

```powershell
.\tools\Build.ps1 -Profile small -FullModel "local/vk102" `
  -Dll "C:\Models\Small\nvngx_dlssnr.dll" `
  -OptiScalerSource $opti -Glslang $glslang -VulkanInclude $vk -Python $py
```

结果：`local/small/` 与 `local/game-package/`。`-FullModel` 指向模型输出 `local/vk102`，不是 `game-package-vk102`。

每个包保留完整的 `manifest/`、`assets/`、`bin/`、`game-files/`、`panel/` 和 `config/`。包目录是运行时资源位置，安装后继续保留。

再次构建时使用新的空输出目录；需要自定义位置可给 `Build.ps1` 传 `-Output` 和 `-PackageOutput`。把新包验证完再替换旧安装，旧包同时保留为恢复入口。

## 5. 配置双后端

一键安装已自动完成双后端配置，默认Full启动，无需重复本节。正常退出游戏后，以下手动可选示例以Small为启动后端，同时注册Full：

```powershell
.\.venv\Scripts\python.exe local/game-package/panel/configure_game.py `
  --package local/game-package `
  --game-bin "C:/Games/Cyberpunk2077/bin/x64" `
  --vk102-manifest local/game-package-vk102/manifest
```

游戏路径中应包含 `Cyberpunk2077.exe` 和 `OptiScaler.ini`。此命令将包内控制面板、模型、INI 和状态路径指向实际位置。

只用 Full 时：

```powershell
.\.venv\Scripts\python.exe local/game-package-vk102/panel/configure_game.py `
  --package local/game-package-vk102 `
  --game-bin "C:/Games/Cyberpunk2077/bin/x64"
```

以 Full 启动并注册 Small 时，在这个命令后增加 `--small-manifest local/game-package/manifest`。

## 6. 安装游戏文件

先备份游戏 `bin/x64` 下这些同名文件：

```text
nrvk_bridge.dll
plugins/CyberpunkNRBridge.asi
plugins/nr_bridge.ini
plugins/nr_hdr_proxy.hlsl
plugins/NRB-deferred-identity.enable
```

将选定启动包的 **game-files 目录内部内容**复制到游戏 `bin/x64`。Small 启动使用 `local/game-package/game-files/`，Full 启动使用 `local/game-package-vk102/game-files/`。只复制一个启动包，另一个通过已注册的 manifest 加载。

配置命令负责生成路径，复制步骤负责实际安装。包移动后重新执行配置，再复制生成的 `plugins/nr_bridge.ini`。两套模型的 component、manifest、权重与 shader 保持各自成套。

## 7. 启动并确认挂载

运行启动包内 `Launch-NR-Panel-Game.cmd` 打开面板，再从游戏平台启动赛博朋克。脚本只启动面板，使用 Windows Python Launcher（`py -3`）；也可在包目录执行 `python panel/nr_control_panel.py`。面板默认地址是 `http://127.0.0.1:8788/`。原生模式按游戏实际输入R运行NR，N=R。显示分辨率、DLSS挡位共同决定R；1080p/1440p/2160p输出都不再要求固定720p NR输入。安装器用户运行结束时打印的Open-NR-Panel.cmd，手动组包用户运行包内Launch-NR-Panel-Game.cmd。

进入存档后检查：

1. 面板列出已注册的 `VK102 310.8`（优化 Full，状态键 `102`）与 `Small 310.9.16`（新版 Small）。
2. 当前后端显示已挂载，处理计数持续增加。
3. 改变风格或强度后，画面对应更新。
4. 切换后端后，实际挂载状态更新，处理继续运行。
5. 原生模式检查R=N；切换固定高度后检查N对应缩小、O不变，再返回原生。

后端切换发生在帧边界，同时重置历史。选择器的保存动作与实际挂载状态分别显示，因此以运行状态和处理计数确认结果。

## 8. 面板操作与性能读数

外观项逐帧生效，无需重新构建模型。OptiScaler 伪装预设则在退出游戏后修改，并在重启后生效。已有配置会保留备份。

| 读数 | 用途 |
|---|---|
| Full / Small 挂载状态 | 确认实际执行模型 |
| processed / 处理计数 | 确认正在处理帧 |
| R / N / O / P | 游戏输入 / NR处理 / 超分输出 / 补边；原生R=N |
| NR network 时间 | 分析后端推理；待时间戳时不以桥内total替代 |
| 面板桥内 total / tail | 分析桥接和跨帧等待 |
| 游戏基础 FPS / 帧周期 | 评价实际吞吐 |
| 帧生成后的 FPS | 评价最终呈现频率 |

对比版本时停在同一存档与视角，保持渲染设置、模型、外观参数和超分/帧生成设置一致。记录基础帧率与网络时间，分别评价后端和流水线的变化。

## 9. 常见问题与处理

| 现象 | 检查与操作 |
|---|---|
| DLL 身份不匹配 | 检查 Full/Small 路径、版本与首页 SHA256 |
| shader 身份检查失败 | 使用 glslang 16.5.0，保留对应源代码和精度配方 |
| 找不到 Vulkan 头文件 | `-VulkanInclude` 指向包含 `vulkan/vulkan.h` 的上级目录 |
| Small 要求 FullModel | 先构建 Full，传 `local/vk102` 模型目录 |
| 面板只显示一个模型 | 重新配置另一包的 manifest，保留对应 component |
| 面板开了但没有处理帧 | 检查实际游戏安装目录、NR 输入尺寸、OptiScaler 路由及桥日志 |
| 移动包后挂载失败 | 在新位置重新配置并复制新生成的桥 INI |
| 8788 已占用 | 退出旧面板，或启动前设置 `NR_PANEL_PORT` |
| 更新后需要恢复 | 退出游戏，恢复下节列出的备份与旧包路径 |

## 10. 恢复与卸载

一键安装用户先退出游戏，执行 `py -3 tools/install_cyberpunk.py --restore "<本次安装目录>/INSTALL-RECEIPT.json"`。手动安装用户正常退出游戏，恢复第 6 节备份的同名文件；安装前不存在的文件只移除本次新增项。若修改过 OptiScaler 伪装预设，恢复对应 `OptiScaler.ini` 备份。其他游戏文件和超分组件保持原状。

旧游戏包仍保留时，也可以重新配置旧包，再安装其 `game-files/`。模型权重、二进制和提取结果都位于本地构建目录，不需要上传或分发。

## 11. 二次开发入口

[其他游戏接入](../PORTING.md) 说明输入颜色合同、资源共享、跨 API 同步、历史重置与目标游戏挂钩。后端优化过程见 [完整时间线](optimization/FULL-HISTORY.md) 和 [深度技术复盘](optimization/TECHNICAL-RETROSPECTIVE.md)。
