# Intel DLSS5 NR — DX12 通用

项目现以 **DX12 通用** 命名；OptiScaler 接入已在《赛博朋克 2077》《巫师三》DX12 版与《死亡搁浅 导演剪辑版》中实机使用。下方保留此前 Vulkan 后端与赛博朋克私有桥的开发、构建及性能记录。**历史私有桥的 −5.43 ms 是独立测量，不作为通用 OPT 桥的性能数字。**

## 巫师三：Small / NR 关闭 / Full

用户实机截图，顺序为 **Small → NR 关闭 → Full**，三张均**未开启补帧**。原图完整保留，点击图片可查看原始分辨率。重点观察面部皮肤、头发与盔甲的表现；截图上的瞬时 FPS 不作为同批性能基准。

### Small

![巫师三 — Small，未开启补帧](docs/images/witcher3-small.png)

### NR 关闭

![巫师三 — NR 关闭，未开启补帧](docs/images/witcher3-nr-off.png)

### Full

![巫师三 — Full，未开启补帧](docs/images/witcher3-full.png)

## 后端与历史发布资料

**2026-11-01 可变分辨率新版：** Full / Small 支持游戏实际输入自动匹配，提供原生、720/540/480/360p处理档位，保留已有模型优化与默认流式处理。新增 [一键构建安装](docs/ONE-CLICK-INSTALL.zh-CN.md)（自动下载模型与缺失的官方OptiScaler；FSR XMX不自动部署）、逐文件备份和恢复。已修复原生控制覆盖与模型切换设备生命周期崩溃。[新版验证与成本](docs/MULTIRES-20261101.md)。

**2026-10-29 更新：Full 232 派发（约 17.45 ms）与新版 Small 108 派发（约 9.98 ms）已接入默认构建；同时发布约 5.43 ms 实机帧周期收益的流式桥接。** 详见 [优化过程、方法与验证](docs/optimization/README.md)。当前 Vulkan 大量参考望月 **mochizuki0323 / DLSSNR-AMD** 社区项目，见 [致谢](docs/optimization/AMD-ATTRIBUTION.md)。Small 构建现在需 `-FullModel local/vk102`，先构建 Full。


[English](README.md) · [开发依赖和安装说明（英文）](INSTALL.md)

本项目在 **Intel Arc B580 / Windows x64** 上提供 Vulkan NR 后端、游戏桥、DLL 模型提取与构建工具、控制面板。实机使用的组合是：

**NR + FSR XMX 超分辨率 + XeSS 帧生成。**

NR 负责神经图像处理与风格控制；FSR XMX 负责超分；XeSS FG / XeFG 负责帧生成。这里不是把 XeSS 超分叫作帧生成，也不是宣称整套 NVIDIA DLSS 功能都在本仓库内。OptiScaler 负责相应路由，超分和帧生成的第三方组件由用户另行准备。

**详细文档：** [完整安装与使用教程](docs/USER-GUIDE.zh-CN.md) · [50 ms→17 ms 完整时间线](docs/optimization/FULL-HISTORY.md) · [逐项代码与性能复盘](docs/optimization/TECHNICAL-RETROSPECTIVE.md) · [其他游戏接入](PORTING.md)

**下载：选择[v0.3.0-multires源码版本](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/tag/v0.3.0-multires)，或main获取后续修订。完整说明见[文档目录](docs/INDEX.md)。旧v0.2.0为历史版本。**

[实验工具与用法](tools/experiments/README.md) · [项目恢复与私人备份清单](docs/RECOVERY.md) · [研究源码档案](source/research-archive/README.md)

## 自动下载或使用本地模型 DLL

**不再要求用户已经拥有 VK102 包。** 安装器可下载固定版本的模型容器，也支持提供对应版本的本地 `nvngx_dlssnr.dll`。首次构建需准备下文列出的开发依赖。提取器把 DLL 当数据解析，不加载或执行 DLL。

| 参数 | 模型 | DLL 内资源 | 固定网络 | DLL SHA-256 |
|---|---|---|---|---|
| `-Profile small` | 310.9.16.0 Small | `WEIGHTS_CC_SMALL_FP8` | 76 行 / 108 发 | `2bc19bc09a7b89dd44f73e2ac0108fcc586e2154bdd0830f242f4f040e28789f` |
| `-Profile vk102` | 310.8.0.0 完整模型 | `WEIGHTS_HT` | 152 行 / 232 发 | `8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206` |

Full还接受原版SHA `e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e`：599个张量与165个B580冷资源均已逐项证实与上表Full完全相同。

两个 DLL 请分别放在不同目录。身份不匹配的版本会被拒绝；也支持包含一个对应 DLL 的 ZIP。这里发布已采纳的优化 FP16 Full 与新版 Small；INT8 实验不在此包内。

仓库不提供模型 DLL、权重、提取后的张量、预编译 SPIR-V、自有二进制安装包或第三方游戏 DLL。所有模型资产和本机二进制均由用户在本地生成。

## 当前性能与完整优化历程

B580、1280×720 NR 输入。最新默认后端如下：

| 测量范围 | 结果 |
|---|---|
| Full 网络历史起点 → 当前链头 | **50.821 → 17.450 ms** |
| 最新 Full 最后一轮同批对照 | **18.724 → 17.450 ms（−1.273 ms）** |
| 新版 Small 同批固定输入 | **14.324 → 9.980 ms，108 派发** |
| 流式桥接，赛博朋克同场景帧周期 | **34.81 → 29.38 ms（−5.43 ms）** |
| 对应基础帧率，帧生成前 | **28.74 → 34.04 FPS** |

[完整优化历程：50 多 ms 如何降到现在](docs/optimization/FULL-HISTORY.md) · [下载安装](INSTALL.md) · [其他游戏接入](PORTING.md)

网络行测量 NR 推理；流式行测量游戏帧周期。历史起点和当前链头来自各阶段记录，同批收益在表内单列。面板的跨帧桥内区间用于运行诊断；FPS 使用游戏基础帧率，XeSS FG 呈现帧率另列。上述历史性能批次的NR输入为1280×720；新版实际输入可变，显示输出由后续超分决定。

## 准备开发依赖

- Windows x64、B580 与满足 Vulkan 功能合同的驱动。
- Python 3.12+，在独立虚拟环境安装 NumPy。
- Visual Studio 2022 C++ Build Tools、Windows SDK、CMake 3.24+。
- **glslang 16.5.0**，不能随意更换编译器版本绕过着色器身份检查。
- Vulkan headers：传入的是含有 `vulkan/vulkan.h` 的 `Include` 或 `include` 目录。
- 与当前桥适配的 OptiScaler 源码依赖，固定提交与所需头文件/Detours 库见 [INSTALL.md](INSTALL.md)。

推荐把项目放在较短、可写的英文路径，例如 `C:\Dev\intel-dlss5nr-cyberpunk2077`。现有原生文件接口仍受 Windows 路径长度限制；不要使用过深目录或中文路径。模型 DLL 放在仓库外。

## 一次构建 Small 或 VK102

在仓库根目录执行：

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-model.txt

# 按自己的开发工具安装位置填写。
$py = ".\.venv\Scripts\python.exe"
$opti = "C:\Dev\OptiScaler"
$glslang = "C:\Tools\glslang\bin\glslang.exe"
$vk = "C:\VulkanSDK\Include"

# VK102：生成 local/vk102 与 local/game-package-vk102。
.\tools\Build.ps1 -Profile vk102 -Dll "C:\Models\VK102\nvngx_dlssnr.dll" `
  -OptiScalerSource $opti -Glslang $glslang -VulkanInclude $vk -Python $py

# Small：生成 local/small 与 local/game-package。
.\tools\Build.ps1 -Profile small -FullModel "local/vk102" -Dll "C:\Models\Small\nvngx_dlssnr.dll" `
  -OptiScalerSource $opti -Glslang $glslang -VulkanInclude $vk -Python $py
```

先构建 Full；使用 Small 或双后端切换时再执行 Small 命令。入口会依次完成：DLL 身份检查 → 解包及张量哈希检查 → B580 冷权重重排 → 网络/桥着色器编译及身份检查 → 原生 DLL/ASI 构建 → CPU 检查 → 游戏包装配。

**不会自动安装或启动游戏。** 模型和包输出目录应为新目录或空目录；再次构建可用 `-Output`、`-PackageOutput` 指定另一个位置，不必覆盖已经在使用的包。

VK102 完整链包括 **599 个原始张量文件、165 个冷资源、39 个网络着色器**，不需要旧权重文件或本机输入样本。每个包均包含自己的 `manifest/`、`assets/`、`bin/nrvk_bridge.dll`、`game-files/`、`panel/` 和 `config/`，应整体保留。

## 配置和安装进游戏

先正常退出游戏，保留你已经可用的 OptiScaler、FSR XMX 和 XeSS FG 组件。备份游戏 `bin/x64` 中将被替换的文件及配置；不要删除无关文件。

**只用 Small：**

```powershell
python local/game-package/panel/configure_game.py --package local/game-package --game-bin "C:/Games/Cyberpunk2077/bin/x64"
```

**只用从 DLL 新构建的 VK102：**

```powershell
python local/game-package-vk102/panel/configure_game.py --package local/game-package-vk102 --game-bin "C:/Games/Cyberpunk2077/bin/x64"
```

**两种都用，推荐默认Full启动：**

```powershell
.\Install-Cyberpunk.cmd -GameDirectory "C:/Games/Cyberpunk2077" -FullPackage local/game-package-vk102 -SmallPackage local/game-package
```

下面是手动选择Small启动的可选示例：

```powershell
python local/game-package/panel/configure_game.py --package local/game-package --game-bin "C:/Games/Cyberpunk2077/bin/x64" --vk102-manifest local/game-package-vk102/manifest
```

随后把选定启动包的 **`game-files/` 内部内容**复制到游戏 `bin/x64/`。不要把两个启动包的文件相互覆盖。如果以 VK102 启动，可反过来在 VK102 包中使用 `--small-manifest local/game-package/manifest` 注册新构建的 Small。

配置命令只修改包内路径，不修改游戏文件，不安装超分或帧生成 DLL，并保留已有 NR 风格参数。若移动包目录，需重新配置，再把生成的 `plugins/nr_bridge.ini` 复制到游戏中。

## 面板操作

运行选定启动包中的 `Launch-NR-Panel-Game.cmd` 打开面板，再从游戏平台启动赛博朋克。脚本只启动面板，使用 Windows Python Launcher（`py -3`）；也可在包目录执行 `python panel/nr_control_panel.py`。默认地址 **http://127.0.0.1:8788/**；可通过 `NR_PANEL_PORT` 改端口。本页截图使用 8789，因为当时另一个面板占用了默认端口。

- **后端**：`VK102 310.8` / 状态键 `102` 是当前优化 Full，`Small 310.9.16` 是新版 Small；只显示已注册的模型。选 Small 或 VK102 后保存；桥会在帧边界切换，重置 history，并加载对应包自己的组件和权重。
- **启用、经典/自然/电影、强度、局部结构、局部色调、皮肤/遮罩、自动遮罩、风格强度**：逐帧控制，不会重新构建模型。
- **实况**：检查“已挂载”与持续增长的处理计数。单纯 DLL 加载成功不足以说明正在出帧。
- **切镜和重置**：自行检查加载存档、转动镜头、曝光变化、切场景后的画面恢复。
- **OptiScaler 伪装预设**：退出游戏后修改，并在重启游戏后生效。面板会保留 INI 备份，不提权或改系统环境。

本机组合中，OptiScaler 的 FSR 路由为 `Dx12Upscaler=fsr31`，帧生成接入为 `FGInput=DLSSG`、输出为 `FGOutput=XeFG`。`fsr31` 这个路由名并不自动证明任意 FSR DLL 都使用 XMX；仍需另行安装兼容的 FSR XMX 实现。这里不提供或自动替换这些第三方 DLL。

## 实机效果图

以下均为用户提供的完整原图，未修图。原生对照标注为 **NR 关闭**，不表示同时关闭了超分与帧生成。各图面板保留了后端、启用状态和风格；镜头、人物姿态和光照略有变化，因此是效果展示，不是逐像素对齐的画质 A/B。

### 原生画面／NR 关闭

![原生画面，NR关闭](docs/images/nr-off.png)

### VK102 与 Small 的三种风格

| 风格 | VK102 310.8 | Small 310.9.16 |
|---|---|---|
| 经典 | <img src="docs/images/vk102-classic.png" width="480" alt="VK102经典"> | <img src="docs/images/small-classic.png" width="480" alt="Small经典"> |
| 自然 | <img src="docs/images/vk102-natural.png" width="480" alt="VK102自然"> | <img src="docs/images/small-natural.png" width="480" alt="Small自然"> |
| 电影 | <img src="docs/images/vk102-cinematic.png" width="480" alt="VK102电影"> | <img src="docs/images/small-cinematic.png" width="480" alt="Small电影"> |

点击图片可查看原始分辨率。这里只公开用户明确提供的说明截图，不打包模型输入/输出 dump、本机测试样本或游戏运行日志。

## 回滚与实验版范围

正常退出游戏，恢复备份的 `nrvk_bridge.dll`、`plugins/CyberpunkNRBridge.asi`、`plugins/nr_hdr_proxy.hlsl`、`plugins/nr_bridge.ini`、`plugins/NRB-deferred-identity.enable`。只移除本次新增的文件；若改过伪装设置，恢复对应 OptiScaler INI 备份。不要处理无关的游戏文件。

本发布为 **Experimental**，适用配置为 Windows x64、Intel Arc B580，支持原生输入自动匹配和低分辨率NR档位，原生输入已验证至3840×2160。仓库发布源码与构建配方；`local/`、`build/` 中生成的权重、DLL、SPV 保留在用户本地。

构建验证记录见 [BUILD-VERIFICATION.md](BUILD-VERIFICATION.md)。着色器先检查编译器原始输出，再恢复冻结的 `NoContraction` 装饰并核对最终哈希；脚本没有内嵌预编译 SPIR-V。

模型工具及参考来自 [DLSSNR-AMD](https://github.com/mochizuki0323/DLSSNR-AMD)。许可证和来源见 [LICENSES.md](LICENSES.md)、[PORTING.md](PORTING.md)。本项目不是 NVIDIA、Intel、AMD、CD Projekt Red 或 OptiScaler 的官方产品。
