# 望月 / DLSSNR-AMD 来源与迁移边界

2026-11-01可变分辨率版延续本文的望月项目致谢；新增NR低分辨率处理与残差恢复参考既有项目内Triton路径，Intel几何生成、同步与设备生命周期由本项目实现。FSR XMX是单独安装的超分组件，不作为本仓库发布内容。

本 Vulkan 后端大量参考 [mochizuki0323/DLSSNR-AMD](https://github.com/mochizuki0323/DLSSNR-AMD)（中文讨论中称“望月的 AMD 项目”）。这是社区实现，不代表 AMD 官方贡献、认证或 SDK。

来源包括模型提取/解包及网络结构理解，也包括 shader 中 Swin、attention、FFN、DS/UPS、尾部折叠的组织方式，以及让生产者按下个消费者所需布局输出、减少中间张量落地的思路。仓库 `third_party/dlssnr_amd_model_tools/PROVENANCE.json` 记录了工具快照；该工具快照 commit 不代表后续每项 shader 研究都来自同一个版本。公开代码中保留了原有来源注释，根 LICENSE 与第三方 LICENSE 保留 Copyright (c) 2026 mochizuki0323。

Intel 专项工作是把这些思路落实为 Xe2/XMX 的具体 cooperative-matrix 映射：SG32 的 owner、A/B fragment 方向、权重返回寄存器、SLM 发布/消费、舍入点与驱动调度。AMD 的 wave/MFMA/FP8 packing 不能直接复制到 XMX；C32 的寄存器方案也不能无条件扩展到 C128/C256/C512。

我们没有把 AMD FP8 的近似数学当成此 FP16 包的质量保证。已采纳 FP16 路线保留对应父臂的运算顺序和精度约束，以具体输入下的数值门和同批计时验收。模型版权与 MIT 源码许可分开：用户自行提供支持的模型 DLL，仓库不分发权重或 NVIDIA DLL。
