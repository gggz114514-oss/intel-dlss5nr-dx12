# FSR XMX：当前完整包的安装与设置

v0.4.0-dx12 完整安装包已包含匹配的 FSR XMX DLL 和依赖，不需要额外传入本地 XMX 目录或单独安装 OPT。

1. 从 [Release](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/tag/v0.4.0-dx12) 下载完整 ZIP，解压后运行 Install.cmd；或从整个源码仓库运行 Install-NR.cmd。
2. 选择游戏 EXE，安装器校验整个载荷、备份已存在的文件，并复制匹配组件。
3. 游戏中开启超分，按 Insert 打开 OPT；确认实际选用 FSR 路径。NR、超分、补帧是独立设置，装好 DLL 不等于游戏已经启用了所有功能。
4. 卸载使用同包 Uninstall.cmd，恢复被替换文件。不要把游戏原生 FidelityFX DLL 当成 MOD 手动删除。

新版入口不使用旧脚本的 -FsrXmxPackage、-GameDirectory 或 -ProbeAsi 参数。命令行指定游戏可用 `Install-NR.cmd -GameDir "C:/Games/MyGame/game.exe"`；可选择任意游戏实际 EXE；三款已验证游戏使用专用配置，其余使用通用 DX12 配置。

默认 NVIDIA DXGI／Streamline 伪装仅作用于游戏进程；赛博朋克／巫师三需在游戏里开启帧生成，死亡搁浅导演剪辑版使用 upscaler FG 输入。现有 INI 优先保留，修改伪装后重启游戏。

各 DLL 的许可证随完整包保留，参见 [LICENSES](../LICENSES.md)。开发者重打包见 [DX12-DEVELOPMENT](DX12-DEVELOPMENT.md)，不要把任意版本库混入已校验的包。
