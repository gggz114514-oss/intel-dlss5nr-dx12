# 特别鸣谢：望月与 TheAutomatic 的 AMD 社区项目

当前 DX12 通用桥以 [TheAutomatic/dlss-5-amd-project](https://github.com/TheAutomatic/dlss-5-amd-project) 的 GPL-3.0 宿主为基础，固定版本与覆盖文件见 [宿主来源](../../source/optiscaler-overlay/UPSTREAM.json)。感谢其游戏输入合同、资源生命周期、OPT 面板和通用接入实现。

本 Vulkan 后端大量参考 [mochizuki0323/DLSSNR-AMD](https://github.com/mochizuki0323/DLSSNR-AMD)（中文讨论中称“望月的 AMD 项目”）。这是社区实现，不代表 AMD 官方贡献、认证或 SDK。

来源包括模型提取/解包及网络结构理解，也包括 shader 中 Swin、attention、FFN、DS/UPS、尾部折叠的组织方式，以及让生产者按下个消费者所需布局输出、减少中间张量落地的思路。仓库 `third_party/dlssnr_amd_model_tools/PROVENANCE.json` 记录了工具快照；该工具快照 commit 不代表后续每项 shader 研究都来自同一个版本。公开代码中保留了原有来源注释，根 LICENSE 与第三方 LICENSE 保留 Copyright (c) 2026 mochizuki0323。

Intel 专项工作是把这些思路落实为 Xe2/XMX 的具体 cooperative-matrix 映射：SG32 的 owner、A/B fragment 方向、权重返回寄存器、SLM 发布/消费、舍入点与驱动调度。AMD 的 wave/MFMA/FP8 packing 不能直接复制到 XMX；C32 的寄存器方案也不能无条件扩展到 C128/C256/C512。

我们没有把 AMD FP8 的近似数学当成此 FP16 包的质量保证。已采纳 FP16 路线保留对应父臂的运算顺序和精度约束，以具体输入下的数值门和同批计时验收。模型版权与 MIT 源码许可分开：源码构建使用支持的模型容器；模型与第三方二进制遵循各自上游条款，不由 MIT 重新授权。
