# 新版 NR：一键构建与安装到赛博朋克

可从[v0.3.0-multires源码发布](https://github.com/gggz114514-oss/intel-dlss5nr-cyberpunk2077/releases/tag/v0.3.0-multires)或main下载；安装器不重发OptiScaler包。当前源码构建和安装恢复验收见[BUILD-VERIFICATION](../BUILD-VERIFICATION.md)。

2026-11-01 可变分辨率版保留优化 Full / Small 和默认开启的流式处理，增加游戏实际输入尺寸自动匹配，以及 720 / 540 / 480 / 360p NR 处理档位。原生模式不是720p上限：输入为1080p、1440p、2160p时按对应原生尺寸处理。固定档保持输入宽高比，不把更小输入放大。

## 安装入口

下载仓库 main 分支并解压到长期保留的目录。先安装以下构建依赖：

- Windows x64、Python 3.12+、Git、7-Zip（`7z`在PATH）。
- Visual Studio 2022 C++ Build Tools、Windows SDK、CMake 3.24+（在PATH）。
- Vulkan headers 与 glslang 16.5.0。设置 `VULKAN_SDK`，或在命令中指定路径。

双击根目录 **Install-Cyberpunk.cmd**，输入赛博朋克游戏目录。也可以明确传参：

```powershell
.\Install-Cyberpunk.cmd -GameDirectory "D:\Games\Cyberpunk2077" `
  -Glslang "C:\Tools\glslang\bin\glslang.exe" `
  -VulkanInclude "C:\VulkanSDK\Include"
```

脚本自动下载固定模型容器，核对SHA256，把DLL作为数据提取权重，然后依次构建Full和Small、运行CPU检查、生成双后端配置并安装。下载的模型DLL不会作为代码执行。首次构建需要较长时间；产物保留在仓库的 `local/installer` 下。**安装后不要移动或删除仓库和这两个模型包**，游戏配置通过绝对路径访问它们。

如果已保存支持的模型DLL，传 `-FullDll` 和 `-SmallDll` 即可跳过对应下载。准备好的包也可直接安装，跳过模型下载和编译：

```powershell
.\Install-Cyberpunk.cmd -GameDirectory "D:\Games\Cyberpunk2077" `
  -FullPackage "D:\NR\full" -SmallPackage "D:\NR\small"
```

安装器检查游戏已退出。它不启动游戏，不替换显卡驱动。

## 自动下载范围

模型下载地址和哈希固定在 `tools/fetch_install_models.py`；提取器继续逐文件检查模型张量SHA256。Full采用RHI维护的固定310.8.0容器，Small采用NVIDIA Packman固定 `rtx-remix-ngx_sdk_dlnr@2.7z`。仓库只发布源码，不重新分发模型DLL和权重。

没有OptiScaler时，自动从官方GitHub下载 **v0.9.4** 固定压缩包，并核对SHA256。已有OptiScaler则保留；新装时也不覆盖游戏目录中已经存在的供应商超分、补帧DLL。

**FSR XMX不在自动部署范围。** 已安装的XMX DLL不动；新用户按XMX作者的说明另行安装。脚本也不自动改变游戏内的超分和补帧选择。完整使用组合及社区来源见[使用指南](USER-GUIDE.zh-CN.md)和[望月项目致谢](optimization/AMD-ATTRIBUTION.md)。

## 安装后怎么用

1. 脚本结束时打印本次 `Open-NR-Panel.cmd` 路径；运行它打开面板。
2. 从平时游戏平台启动赛博朋克，使用已配置的OptiScaler链路。
3. 默认 **Full + 原生·自动**，流式处理开启。面板可以切换Small和NR处理分辨率。
4. 进入3D存档后，查看 `R / N / O / P`：游戏实际输入 / NR处理尺寸 / 超分输出 / 网络补边。原生模式应为 **R=N**；固定720p会按比例得到例如1279×720，不代表输出改成720p。
5. 面板显示桥内区间与GPU网络耗时是两个口径，不能把桥内重叠区间当成网络推理时间。

## 备份与恢复

每次安装独立生成 `local/installer/installs/<时间>/INSTALL-RECEIPT.json` 和 `backup`。收据记录覆盖文件前后SHA256；原有文件备份，新添加文件也列入恢复清单。安装过程中复制失败会按本次清单恢复。

退出游戏后执行：

```powershell
py -3 tools/install_cyberpunk.py --restore "<本次安装目录>\INSTALL-RECEIPT.json"
```

恢复只涉及本次安装记录的文件，不删除模型包，不删除游戏存档。

## 新版验证记录

- Full/Small两模型22组原生输入尺寸，七帧反馈/重置/切档功能检查通过。
- 修复Full540p的padding残留，以及降采样过冲破坏强度0恒等的问题。
- 修复旧启动控制把面板“原生0”覆盖成720；普通与流式路径使用同一有效控制。
- 修复Full/Small切换后复用旧Vulkan设备函数指针导致的崩溃。两模型共6次上下文生命周期、42帧与720p基线逐字节一致。
- 从公开源码实际完成Full→Small全流程构建，后端5项、桥11项、面板和组包身份检查通过。

720p历史性能仍为Full约17.45ms、Small约9.98ms；这些数字不是高分辨率性能承诺。降采样档位与静态图对比记录见[可变分辨率版本说明](MULTIRES-20261101.md)。

## 第三方来源与分发范围

本仓库不上传、镜像或重新打包OptiScaler Release。安装脚本让用户从[OptiScaler官方v0.9.4](https://github.com/optiscaler/OptiScaler/releases/tag/v0.9.4)直接下载，保留包内许可文件。OptiScaler源码许可为[GPLv3](https://github.com/optiscaler/OptiScaler/blob/7534ad00bf9e590eedb99e8dd9fd8c89dae3654f/LICENSE)，其第三方组件适用各自许可。FSR XMX不在本仓库发布和自动部署范围。模型容器同样仅下载到用户本机缓存，不进入Git或Release附件。
