# 历史研究：赛博朋克私有桥的流式提交与退休

本文仅记录旧赛博朋克私有桥的实现与原始测量，不描述 v0.4.0-dx12 OPT 通用版的当前开关、默认值或性能。当前用户安装请看 [安装指南](../../INSTALL.md)。历史源码位于 source/bridge，当前完整包不安装该私有 ASI。

这一项属于 D3D12/Vulkan 桥接提交与退休，不是 NR 神经网络内部提速，必须与 Full/Small 内核优化分别记账。

同一游戏生命周期、同一静止 3D 场景，SYNC/LATELATCH 热切换四窗口共 1,067 帧。历史测量：帧周期约 **34.81→29.38 ms**，约 **28.74→34.04 FPS（+18.4%）**；帧间 gap 约 **14.65→9.32 ms**，减少约 **5.33 ms**；CPU tail_wait 平均约 **25.30→4.18 ms**。逐窗口记录见 `streaming-windows.json`。

实现重点是让 CPU 不必在当帧提交尾部立即等全部 GPU 工作完成：保留 GPU 队列依赖，延后回收已提交工作，在真正需要复用资源前检查退休。`source/bridge/src/deferred_identity.cpp` 默认 `g_diag_late_latch{1}`；`nr_gpu_handoff.cpp` 与 Vulkan processor 负责跨 API 交接。资源不能在消费者完成前重写，流式处理不是删掉正确性所需的 fence。

历史源码保留该实现及 GPU handoff／资源生命周期配套变更，供研究恢复。该批超分实际为 **FSR XMX**；旧日志字段 `d3d_deferred_sr` 或历史 XeSS 命名不代表 XeSS SR。`vk_network_266` 也是历史计时字段名，不代表当前 Full 仍有 266 派发。

## 使用与测量口径

当时的私有桥构建默认启用该流式路径、关闭诊断采集；此配置不作为当前通用包的设置说明。上述数据来自同一静止场景的游戏帧周期；网络内核耗时单独记录。资源复用以 GPU 完成状态为准，保留跨 API 依赖和历史重置。当前安装与恢复见 [INSTALL.md](../../INSTALL.md)。
