# FP16 优化记录 / FP16 optimization record

当前用户版本为 **v0.4.0-dx12**：OPT 通用桥、Full／新 Small、原生自动与 720/540/480/360p 处理档位。[安装入口](../../INSTALL.md) · [当前构建与验证](BUILD-AND-VALIDATION.md) · [Small 稳定性经验](SMALL-GPU-HANG-20261103.md)。

本目录保留各轮优化的日期、父臂和 720p 计时口径。Git 保存源码、方法与清单；完整用户安装包在 Release，包含匹配的运行时、模型及依赖。诊断采集帧和私人日志不进入公开源码。

**特别鸣谢两个 AMD 社区项目：** [望月 DLSSNR-AMD](https://github.com/mochizuki0323/DLSSNR-AMD) 的网络组织、生产消费布局与模型工具，以及 [TheAutomatic/dlss-5-amd-project](https://github.com/TheAutomatic/dlss-5-amd-project) 的 OPT 通用宿主与游戏接入。Intel XMX 的 lane/fragment、供料和生命周期按 B580 实现。[来源与许可边界](AMD-ATTRIBUTION.md)。

## 完整演进

**Full 从 50.821 ms 优化到 17.450 ms，278→232 派发。** 阅读 [完整优化历程](FULL-HISTORY.md)，从第一轮 AMD 方法适配、供料与生命周期，到 VK102、C32 整链、全后端推广。下方 13 步表是最后一轮，不是整个项目的起点。

## 当前保留的网络与历史计时

| 入口 | 发布源码 | 规模 | 历史实测口径 |
|---|---|---|---|
| `-Profile vk102`（Full） | `source/network-fp16-20261027` | 152 行、502 buffers、232 派发、39 shader | handoff 18.724 → 17.450 ms，同批节省 1.273430 ms |
| `-Profile small`（新版） | `source/network-small-20261028` | 76 行、222 buffers、108 派发、39 shader | 修正范围的旧 Small 14.324 → 9.980 ms，同批固定输入 |

**Small 就是新的约 9 ms 版本，不是旧 118 派发版本。** 两个旧源码目录仍保留作为历史来源和少量共用模块；当前 profile 均指向新目录。Small 不是 Full 权重直接减半：保留自身模型权重、五个偏移专用核、范围修正和新的计划接线。

Full 原研究计划 SHA256：`bc34404c00a52a02b5be77cbb167e71cb3965536f634ecd1bed663c002e45df6`。
Small 原研究计划 SHA256：`cf1d1fac922c33e31793a4a1c828364577c40fb26bef8ff5a6b6a23c5cdaf80b`。
发布计划使用相对路径和运行时输入；shader、权重、PUSH、绑定、网格、POLICY、HISTORY 已逐项核对，原研究身份与发布身份分别记录。

## 如何阅读

- [深度技术复盘](TECHNICAL-RETROSPECTIVE.md)：从原数据流到具体实现，展开十二个技术主题并链接当前源码。
- [完整安装教程](../USER-GUIDE.zh-CN.md)：完整安装包的安装、设置、卸载与日志导出。

- [优化方法与过程](METHODS.md)：删交接、保留并行、控制寿命及失败后如何追查实现。
- [最后一轮逐步收益](ACCEPTED-STEPS.md)：2026-10-27 全后端 13 步采纳与组合结果。
- [构建与验证](BUILD-AND-VALIDATION.md)：重建精度约束、Small 特殊资源、验证范围。
- `full-confirm6.json`、`small-timing.json`：对应历史网络批次的原始汇总。私有桥研究另存于标注日期的历史专题。

测量分为网络时间、游戏帧周期和构建复现三类，详细协议与版本身份见对应记录。

## English summary

The default profiles now select the accepted 232-dispatch Full and **108-dispatch optimized Small**, not the legacy 118-dispatch Small. The current Vulkan implementation substantially references **mochizuki0323 / DLSSNR-AMD**, a community MIT project, including network organization and producer/consumer design. Credit is not limited to model extraction tools.

The Full campaign's direct paired gain is 1.273430 ms (95% CI 1.251966–1.294894). Optimized Small measured 9.980 ms in its fixed-input batch. Build reproduction and historical GPU acceptance are distinct; see the linked records for exact boundaries.
