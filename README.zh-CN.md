# Intel DLSS5 NR — DX12 通用

**Intel Arc 上的 Vulkan NR Full / Small + OptiScaler 通用桥。** 已在《赛博朋克 2077》《巫师三》DX12 版、《死亡搁浅 导演剪辑版》中实机使用。设置集成在游戏内 OPT 面板，按 **Insert** 打开。

[下载完整安装包](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/download/v0.4.0-dx12/Intel-NR-DX12-20261104.zip) · [v0.4.0 发布说明](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/tag/v0.4.0-dx12) · [安装与卸载](INSTALL.md) · [English](README.md)

## 下载后怎么用

1. 下载 **Intel-NR-DX12-20261104.zip**，完整解压。
2. 退出游戏，双击 **Install.cmd**，选择游戏 EXE；无需预装 OPT、Python、Visual Studio 或 Vulkan SDK。
3. 启动游戏并开启 DLSS/超分，进入存档后按 **Insert**，在 Intel NR 页切换 Full / Small、处理分辨率及风格。
4. 卸载时退出游戏，双击同一包里的 **Uninstall.cmd**，选择同一 EXE。安装器恢复原文件，保留备份和用户日志。

也可以下载本仓库源码后双击 **Install-NR.cmd**：脚本从 Release 下载固定版本完整包、核对 SHA256，再打开相同安装器。下载整个仓库，不要只保存单个 CMD。

| 游戏 | 选择的 EXE |
|---|---|
| 赛博朋克 2077 | `bin/x64/Cyberpunk2077.exe` |
| 巫师三次世代版 | `bin/x64_dx12/witcher3.exe`，使用 DX12 |
| 死亡搁浅 导演剪辑版 | `ds.exe` |

Windows 10/11 x64，当前实测硬件 Intel Arc B580。显卡驱动由用户安装；缺少 VC++ x64 运行库时安装器从微软下载并校验签名。其他游戏的接入见 [通用桥与兼容说明](PORTING.md)。

其他游戏可直接选择实际渲染 EXE，使用通用 DX12 配置（`dxgi.dll`），不再限制三款游戏。通用配置先关闭补帧，确认 NR 生效后再配置；未加载时使用 **Collect-Logs.cmd** 导出日志。安装成功与游戏兼容验证分开记录。

## 本版包含什么

- 已优化的 **Full / 新 Small**，可变原生输入分辨率，以及 720 / 540 / 480 / 360p NR 处理档位。原生跟随游戏实际渲染输入，不等于显示器输出分辨率。
- 配套 OPT 宿主、NR 运行时、模型资源、FSR XMX 和超分／补帧依赖。整包放在 Release，Git 保存源码、构建工具与清单。
- Small 的 **4–64 KiB 窄缓冲 4 KiB 对齐修复**，保留原有模型、shader 与计算顺序。
- 参考望月实现的输入编码／颜色、运动矢量与深度合同；游戏内 Full/Small、分辨率和风格控制。
- 经典／自然／电影与强度修复：自然风格移除造成过暗的旧 LUT，1 附近连续过渡。已移除自定义滤镜编辑器。
- 默认 Full、原生、经典；干净安装开启游戏进程内 NVIDIA DXGI/Streamline 伪装与 XeFG 输出。赛博朋克／巫师三还需在游戏中开启帧生成；死亡搁浅自动选 upscaler 帧生成输入。已有 INI 优先保留。
- 安装前完整校验、逐文件备份、按安装记录卸载、日志一键导出。**不要手动删除游戏自带 FidelityFX DLL。**

## 特别鸣谢 AMD 社区的两个项目

- **[望月 / mochizuki0323 — DLSSNR-AMD](https://github.com/mochizuki0323/DLSSNR-AMD)**：模型解包、网络组织、生产消费布局、矩阵之间的交接与输入处理为本 Vulkan 后端提供了重要基础。
- **[TheAutomatic — dlss-5-amd-project](https://github.com/TheAutomatic/dlss-5-amd-project)**：OPT 通用宿主、游戏输入合同、提交／资源生命周期和面板集成是本次通用桥的重要参考与源码基础。

这是社区项目之间的借鉴与移植，不代表 AMD／Intel／NVIDIA 官方产品。望月 MIT、OPT 派生宿主 GPL-3.0 及各第三方许可证分别保留，见 [许可证与来源](LICENSES.md) 和 [详细致谢](docs/optimization/AMD-ATTRIBUTION.md)。

## 巫师三实机对比

用户确认顺序：**Small → NR 关闭 → Full**，三张均**未开补帧**。原始 PNG 未调色、未缩放；可点击查看原图。观察皮肤、头发和盔甲的表现，截图瞬时 FPS 不作为同批性能基准。

### Small

![Small，无补帧](docs/images/witcher3-small.png)

### NR 关闭

![NR 关闭，无补帧](docs/images/witcher3-nr-off.png)

### Full

![Full，无补帧](docs/images/witcher3-full.png)

## 从 50 ms 到现在

Intel Arc B580、1280×720 NR 输入的历史记录：

| 项目 | 实测记录 |
|---|---|
| Full 网络起点 → 优化链头 | **50.821 → 17.450 ms** |
| 最新 Full 优化批次配对 | **18.724 → 17.450 ms** |
| 新 Small 配对 | **14.324 → 9.980 ms** |

以上为 NR 网络的历史计时。处理分辨率、游戏负载与驱动会改变耗时。完整经历见 [50 ms→17 ms 历史](docs/optimization/FULL-HISTORY.md)、[优化方法](docs/optimization/TECHNICAL-RETROSPECTIVE.md)。

## 开发、排障与恢复

- [DX12 宿主源码、固定上游及构建](docs/DX12-DEVELOPMENT.md)
- [Small 挂死：完整排查、修复与经验笔记](docs/optimization/SMALL-GPU-HANG-20261103.md)
- [新包身份与文件 SHA256](docs/releases/20261103-package-manifest.json)
- [实验工具](tools/experiments/README.md)、[恢复说明](docs/RECOVERY.md)、[文档索引](docs/INDEX.md)

故障时运行 **Collect-Logs.cmd**（源码入口为 `Collect-NR-Logs.cmd`），检查生成的 ZIP 后反馈。日志不会自动上传，不含存档或采集帧。请附游戏、显卡、驱动、后端、输入分辨率和复现动作。
