Intel NR DX12 v0.5.0 — 安装与卸载

Windows 10/11 x64，兼容的 Intel 显卡与驱动。无需预装 OPT、Python、Visual Studio 或 Vulkan SDK。
本版在 Intel B580 上通过燕云国服 DLSS/FSR XMX、Full/Small NR 和 DLSS-G 转 XeFG 场景实测。

1. 完整解压 ZIP，退出游戏，运行 Install.cmd，选择实际 DX12 渲染 EXE。
   任意 EXE 均可选择；赛博朋克、巫师三 DX12、死亡搁浅导演剪辑版、燕云国服（yysls.exe）和国际服（wwm.exe）保留专用配置。
   燕云选择 Engine/Binaries/Win64rh/yysls.exe，在游戏设置中开启“NVIDIA 插帧”。
   燕云补帧使用 DLSS-G 输入转 Intel XeFG，开关和倍率跟随游戏。其他未知游戏补帧初始关闭。
   缺少 VC++ x64 运行库时从微软下载签名安装器。
2. 启动游戏并开启 DLSS/超分，按 Insert 打开 OPT。
   NR 只使用 Intel；Full/Small、原生或 720/540/480/360p、经典/自然/电影保留。
   默认 Full/原生/经典。旧 daniel/lmxxf/mochizuki/auto 迁移为 intel，off/none 保持关闭。
   已有 SR/FG、Full/Small 与插件配置保留。游戏内“启用 NR”可独立关闭。
3. FSR/DLSS XMX 都通过 FFX/FSR 超分路径运行。
   在 OPT 的 FSR 版本列表选择 4.0.2X 或 DLSS-EX/FX/EFX/KX/LX/MX，再应用并重建超分。
   不要选择仅供 NVIDIA GPU 的原生 DLSS 后端。包内保留 fsr-original.dll；
   NVIDIA 原版 nvngx_dlss.dll 提供 DLSS SR 模型。NR 与 SR 模型是独立设置。
4. 默认 NVIDIA DXGI/Streamline 伪装仅作用于游戏进程，修改后重启。
   包内 AmdSpoofing.asi 为 Overclockers 原插件的 Intel 专用二进制适配，识别 Intel 来源显卡。
   修改身份与原作者发布页见 payload/Licenses/AmdSpoofing-SOURCES.txt 和 PATCH.json。
   通过 plugins 或既有相对自定义插件目录加载；原作者未背书此适配。
   Plugins/LoadAsiPlugins=false 仍会关闭 ASI 加载。其他 OPT 插件可自行添加，无包白名单。
   同时有多个可识别的 OPT 代理时提示处理；其他代理保留，按对应 MOD 说明配置串联。
5. 更新前先用原安装包 Uninstall.cmd 卸载，再安装新版。不要手删原生 FidelityFX/nvngx DLL。
   安装器逐文件备份并记录 intel-nr-install.json。卸载恢复未被用户再修改的本包文件；
   用户新增文件和已替换文件保留，旧版本在 intel-nr-backups 中。备份和日志不清空。
6. Collect-Logs.cmd 导出本地日志 ZIP，不自动上传；日志可能含本机路径，不含存档和截图。
   不兼容时保存日志并退出游戏，不修改 TDR 或驱动规避问题。

宿主 GPL-3.0、项目 MIT 和第三方许可分别保留于 SOURCE-LICENSES、payload/Licenses。
感谢 mochizuki0323/DLSSNR-AMD（望月，MIT）与 TheAutomatic/dlss-5-amd-project（GPL-3.0）。
XMX 来源由用户确认为 OptiScaler Discord 频道，作者未公开身份信息，文件未附独立许可；没有按根 MIT 重许可。
ASI 作者来源是 Overclockers 的 amd_spoofing_enhance_0.0.1 发布页，页面未附独立许可证。
构建和测试通过不等于所有游戏兼容；Full ~17ms/Small ~9ms 为历史平台测量。
VC++ 官方来源：https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist

新版 Small 色彩实现修复已通过交接记录中的离线数值、实际 DLL 与五组媒体验收；游戏内观感尚待复验，未换装游戏。
New Small color implementation fix passed the supplied offline numerical, actual-DLL and five-media acceptance; in-game visuals remain unverified. No game deployment.
Full、宿主和补帧通道保持已实测版本；旧版回退方案保留旧 Small。
Full, the host and FG routing retain their tested versions; the legacy rollback variant keeps its earlier Small.
