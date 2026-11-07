# DX12 通用版安装与卸载

## v0.7.2 统一安装器

1. 下载并完整解压 ZIP，退出游戏，再运行 `Setup.cmd`。
2. 选择实际的 DX12 渲染 EXE。默认使用增强 ASI 挂载，也可选内置伪装。两种方式使用相同的宿主、Full 和 Small。
3. 安装前先检查旧 OPT，确认后卸载旧安装并写入当前文件。单独卸载可在同一窗口选择「检查并卸载」。
4. 备份与日志位于实际游戏 EXE 同目录的 `intel-nr-backups`，完成窗口会显示此次操作的子目录。
5. 游戏中按 Insert 打开 NR 菜单。勾选「启用残差外推」，输入 1–4 倍并按回车。新配置默认关闭，倍率预填 2。保存设置可保留两项。

燕云国服选择 `Engine/Binaries/Win64rh/yysls.exe`。巫师三选择 `bin/x64_dx12/witcher3.exe`。新版界面交互与画面待实机复验。

[下载 v0.7.2](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/tag/archive-v0.7.2-controls) · [版本归档](docs/ITERATION-ARCHIVE.md)

## 原 v0.4.0 安装方式

[下载完整安装包](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/download/v0.4.0-dx12/Intel-NR-DX12-20261104.zip) · [发布说明](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/tag/v0.4.0-dx12)

Intel NR + FSR XMX — 新用户安装指南

你只需要游戏、兼容的 Intel 显卡及已安装的显卡驱动。
安装包已带配套 OptiScaler、Full／Small 模型和运行时、FSR XMX、超分与补帧依赖。
不需要先安装 OPT，不需要 Python、Visual Studio、Vulkan SDK，也不需要复制开发机文件。

一、准备
Windows 10/11 64 位。当前性能及稳定性测试平台为 Intel Arc B580。
安装器可选择任意游戏 EXE。下列三款保留已验证的专用配置；其他游戏使用通用 DX12 配置（dxgi.dll 入口），不再受名单限制：
  巫师三次世代版：bin\x64_dx12\witcher3.exe（必须以 DX12 启动）
  赛博朋克2077：bin\x64\Cyberpunk2077.exe
  死亡搁浅导演剪辑版：ds.exe
先退出游戏。将整个压缩包解压到一个普通目录，不要在压缩包内直接运行，不要只取出 Install.cmd。
如果曾装过其他 OPT、ReShade 或代理 DLL MOD，先用该 MOD 的卸载方式清理；不要直接删除游戏自带 DLL。
尤其不要把 amd_fidelityfx_loader_dx12.dll、amd_fidelityfx_framegeneration_dx12.dll、amd_fidelityfx_upscaler_dx12.dll 当成 OPT 专属文件删除：赛博朋克本身也使用它们。安装器会备份已存在的文件，卸载时恢复。

二、安装（鼠标操作即可）
1. 双击 Install.cmd，在窗口中选择游戏实际渲染 EXE，不要选择启动器。
2. 等待文件校验及复制。缺少 VC++ x64 运行库时，脚本从微软下载签名安装器；按窗口完成安装。
   只有缺少运行库时才需要联网。显卡驱动须通过 Intel 或电脑厂商提供的安装器安装。
3. 看到“安装完成”后关闭窗口。游戏目录无写权限时，右键 Install.cmd 选择“以管理员身份运行”。
   安装器不会自动修改显卡驱动、频率、Windows TDR 或游戏存档。

三、第一次进入游戏
从 Steam／Epic 等原入口启动游戏；巫师三选择 DX12。
在游戏画面设置中启用 DLSS/超分，进入正常 3D 存档场景后按 Insert 打开 OPT 面板。
进入 Intel NR 设置，确认启用；默认 Full、原生 NR 输入、经典风格。
Full 偏重质量；Small 更快。处理分辨率可以单独调整，原生表示跟随游戏送入的渲染分辨率，不是显示器分辨率。
超分选择 FSR（本包提供 XMX DLL）；以面板和日志中的实际加载状态为准。
干净安装默认开启游戏进程内的全局 DXGI NVIDIA 伪装和 Streamline 伪装（RTX 4090），以及 XeFG 补帧输出；不修改系统显卡驱动或全系统设备身份。
赛博朋克／巫师三默认 DLSSG 输入，还需在游戏画面设置中启用帧生成；死亡搁浅导演剪辑版没有原生 DLSSG，安装器自动选择 upscaler 输入。
其他游戏使用 dxgi.dll 通用入口，补帧初始关闭，FGInput=upscaler；确认 NR 和超分生效后再按游戏配置补帧。通用入口未加载时导出日志，不自动猜测兼容修复。
如从旧配置升级，已有配置优先；请在 OPT 面板核对伪装与补帧选项，修改伪装后重启游戏。补帧实际启用状态以游戏面板及日志为准。
自然／电影与经典的差异受“局部色调”影响，可先设为 1；风格强度 0–1 混合，1–2 增强曲线。
此包没有网页控制面板或自定义滤镜编辑器，设置在游戏内 OPT 面板中完成。
已有 OptiScaler.ini 会保留，已有用户设置不会被默认值覆盖。

四、卸载／更新
退出游戏，双击 Uninstall.cmd，选择同一 EXE。
卸载器只处理安装记录中的文件，恢复被替换的旧文件；保留备份、日志和非本包文件。
备份在游戏 EXE 目录的 intel-nr-backups；安装记录为 intel-nr-install.json，请勿手动删除。
更新此包请先卸载再安装。卸载不会撤销系统 VC++ 运行库。

五、遇到问题
双击 Collect-Logs.cmd，选择游戏 EXE，得到本地 ZIP；查看后再发给维护者。
ZIP 包含 OPT 日志、配置、NR 状态、安装记录及显卡驱动信息，可能包含本机路径；不含存档、截图或模型输入帧，也不会自动上传。
安装日志和诊断 ZIP 位于 %LOCALAPPDATA%\IntelNR\InstallerLogs。
不启动／无面板：确认游戏 EXE 选择正确、DX12 模式、压缩包完整解压及驱动已安装。
NR 不生效：确认游戏超分已启用，再导出日志，不要叠装多个版本碰运气。
卡顿／NR 暂停：保存日志，退出游戏；不要靠增大 TDR 或反复切换后端掩盖故障。

六、版本内容
配套宿主与着色器；Full、新 Small；Small 窄缓冲 4KB 对齐稳定性修复；望月输入归一化适配；风格强度修复。
自然风格已移除过暗的旧 LUT，保留轻度曝光／对比度与饱和度调整；风格强度 1 不再完整应用旧压暗曲线。
Vulkan 后端的生产消费优化和通用输入接入大量参考 AMD 望月项目及其 OptiScaler 集成，相关上游许可保留在 Licenses 中。
本次安装器升级不改变模型、着色器与推理代码，不把旧游戏测试视为新版实机验收。
“面向新用户”是指无需开发环境，不是无条件兼容所有硬件与游戏。
本包使用用户提供的模型及第三方 FSR XMX 文件，保留 Licenses。公开发布需分别遵循各组件的分发条件；本次未上传公开仓库。

VC++ 官方来源：https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist


## 从仓库启动

下载并解压整个源码包，双击 `Install-NR.cmd`。脚本按 `tools/intel-nr/PACKAGE.json` 下载固定版本和 SHA256；解压后再次校验每个 payload 文件。无需本地编译。`Uninstall-NR.cmd` 和 `Collect-NR-Logs.cmd` 使用同一安装器。

## 开发者

源码构建请看 [DX12-DEVELOPMENT](docs/DX12-DEVELOPMENT.md)。`tools/Build.ps1` 及旧私有桥仍保留供开发研究，普通用户不需要运行。
