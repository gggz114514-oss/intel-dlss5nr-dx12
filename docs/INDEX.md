# 文档目录与版本口径 — 2026-11-01

当前代码版本为 **v0.3.0-multires**，main继续提供文档和修订。默认Full＋原生自动＋流式开启；Small是优化后的108派发版。固定720/540/480/360p是降低NR处理尺寸的选项，不是原生输入上限。

首次安装读[一键安装](ONE-CLICK-INSTALL.zh-CN.md)，日常使用读[完整指南](USER-GUIDE.zh-CN.md)，维护构建读[验收记录](../BUILD-VERIFICATION.md)，其他游戏读[移植指南](../PORTING.md)。

## 统一口径

- 游戏输入R、NR处理N、padding P、超分输出O独立描述；原生R=N。
- Full17.45ms、Small9.98ms是历史B580/720p网络计时；流式约5.43ms是同场景游戏帧周期收益，两者不混加。
- 安装器下载模型容器并本机构建；OptiScaler仅从官方Release下载，不由本仓库重新上传。FSR XMX不自动部署，已有SR/FG文件保留。
- Full支持两个已证明权重等价的精确DLL SHA；Small保持自身模型和偏移核。二进制版本文字不替代哈希。
- 发布物是源码；历史截图为展示资产，不是新多分辨率连续运动验收。

## 全部维护文档

| 文档 | 状态 |
|---|---|
| [Local assets](../ASSETS.md) | 当前v0.3.0-multires说明 |
| [Current source-build verification — 2026-11-01](../BUILD-VERIFICATION.md) | 当前v0.3.0-multires说明 |
| [Build and install](../INSTALL.md) | 当前v0.3.0-multires说明 |
| [Licenses and provenance](../LICENSES.md) | 当前v0.3.0-multires说明 |
| [接入其他游戏 / Integrating another game](../PORTING.md) | 当前v0.3.0-multires说明 |
| [Intel DLSS5 NR — Cyberpunk 2077](../README.md) | 当前v0.3.0-multires说明 |
| [Intel DLSS5 NR — 赛博朋克 2077](../README.zh-CN.md) | 当前v0.3.0-multires说明 |
| [Variable-resolution FP16 update — 2026-11-01](../RELEASE-NOTES.md) | 当前v0.3.0-multires说明 |
| [可变分辨率版 — 2026-11-01](MULTIRES-20261101.md) | 当前v0.3.0-multires说明 |
| [新版 NR：一键构建与安装到赛博朋克](ONE-CLICK-INSTALL.zh-CN.md) | 当前v0.3.0-multires说明 |
| [换电脑后的项目恢复手册](RECOVERY.md) | 当前v0.3.0-multires说明 |
| [从下载到赛博朋克实机：完整使用指南](USER-GUIDE.zh-CN.md) | 当前v0.3.0-multires说明 |
| [已采纳步骤与收益口径](optimization/ACCEPTED-STEPS.md) | 历史证据，保留原始批次；已链接新版 |
| [望月 / DLSSNR-AMD 来源与迁移边界](optimization/AMD-ATTRIBUTION.md) | 当前v0.3.0-multires说明 |
| [当前构建与验证 — 2026-11-01](optimization/BUILD-AND-VALIDATION.md) | 当前v0.3.0-multires说明 |
| [Source-package verification](optimization/BUILD-VERIFICATION-20261024.md) | 历史证据，保留原始批次；已链接新版 |
| [从 50.82 ms 到 17.45 ms：Vulkan FP16 完整优化历程](optimization/FULL-HISTORY.md) | 历史证据，保留原始批次；已链接新版 |
| [优化过程与可复用方法](optimization/METHODS.md) | 当前v0.3.0-multires说明 |
| [FP16 优化记录 / FP16 optimization record](optimization/README.md) | 当前v0.3.0-multires说明 |
| [跨帧流式处理：约 5.43 ms 的实机收益](optimization/STREAMING.md) | 当前v0.3.0-multires说明 |
| [Vulkan FP16 深度技术复盘：从算得出来到持续供给 XMX](optimization/TECHNICAL-RETROSPECTIVE.md) | 历史证据，保留原始批次；已链接新版 |
| [Accepted optimized FP16 source](../source/network-fp16-20261027/README.md) | 当前v0.3.0-multires说明 |
| [Accepted optimized FP16 source](../source/network-small-20261028/README.md) | 当前v0.3.0-multires说明 |
| [实验工具：构建、回放、数值、性能与恢复](../tools/experiments/README.md) | 当前v0.3.0-multires说明 |

## 历史档案与上游文档

`source/research-archive`是带哈希的历史档案，原始报告、失败记录和方法按发生时版本保留；其中命令不作为当前安装步骤。`third_party`中的上游说明和许可证保留原文，更新本项目时不篡改。旧JSON计时记录也保持原始数值，最新功能与验收以本目录列出的现行说明为准。
