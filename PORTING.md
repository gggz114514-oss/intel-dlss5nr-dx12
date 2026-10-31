# 接入其他游戏 / Integrating another game

本项目分为可复用的 Vulkan NR 后端与赛博朋克 D3D12 游戏桥。接入新游戏时复用网络计划、模型包和 Vulkan 执行器，针对目标引擎实现输入采集、队列交接、结果合成与历史重置。赛博朋克 ASI 是游戏适配层，不是通用游戏插件。

## 代码入口

| 部分 | 代码 | 接入工作 |
|---|---|---|
| NR Vulkan 执行 | `source/vulkan/port_network_bridge_backend.cpp` | 保留模型计划、权重布局、PUSH 和派发语义 |
| 输入/输出转换 | `source/vulkan/port_network_bridge_gather.comp`、`port_network_bridge_scatter.comp` | 对齐目标纹理、通道、尺寸和数值域 |
| 处理器与资源寿命 | `source/vulkan/nrb_vulkan_processor.cpp` | 对接外部资源与提交、完成、回收 |
| 游戏挂钩入口 | `source/bridge/src/asi.cpp` | 替换目标游戏的加载和渲染接入点 |
| 延迟提交及合成 | `source/bridge/src/deferred_identity.cpp`、`nr_hdr_proxy.hlsl` | 确定目标引擎中的处理时序与颜色管线 |
| 跨 API 交接 | `source/bridge/src/nr_gpu_handoff.cpp` | D3D12/Vulkan 资源共享、fence、资源状态 |
| 路由接口 | `source/bridge/include/nr_route_adapters.h`、`source/bridge/src/routes/` | 按真实使用的超分路由接入 |
| 配置与状态 | `panel/`、`source/bridge/include/nr_bridge.h` | 模型选择、路径、状态和处理计数 |

## 1. 先定义输入合同

分别定义游戏输入 **R**、NR处理尺寸 **N**、网络补边 **P** 和超分输出 **O**。原生模式N=R，固定档按宽高比缩小到360/480/540/720p且不放大小输入。确认纹理格式、RGB 通道次序、行跨度、有效视口与 padding；按现有 gather/scatter 的数值域接入。区分线性 HDR、曝光处理、色调映射和最终显示颜色。记录模型真正读取的资源及参数，逐项映射目标引擎中的来源。不要仅凭 API 中同名字段直接绑定。

先确定 NR 在超分、后处理、UI 和呈现之间的位置；HUD 是否进入 NR 由接入点决定。保持输入与输出对应同一逻辑帧，并跟踪跨帧历史的身份。

## 2. 共享资源与队列同步

D3D12 接入按共享资源、外部同步和 Vulkan 所需功能建立设备合同。生产者完成写入后，消费者才能读取；合成完成后资源才能复用。跟踪每个资源槽的最后消费者及对应 fence 值，保留资源状态转换与队列所有权操作。

采用已发布 late-latch 的思路：CPU 可以继续提交独立工作，在复用资源槽前退休上一批工作。不要把同一槽连续交给尚未完成的两帧。纹理重建、窗口变化、退出时先完成相应资源退休，再释放或重绑。

## 3. 历史与切换

载入存档、切场景、分辨率变化、资源重建和后端切换都需要走明确的历史重置路径。Full 和 Small 每个包保留自己的 component、manifest、权重和 shader；在帧边界切换，保持包身份一致。

## 4. 推荐接入顺序

1. 接通原图直通路径，验证资源格式、画面位置、颜色和 HUD。
2. 挂载一个 FP16 后端，固定镜头验证输入与输出，检查处理计数和实际模型身份。
3. 验证转镜、曝光变化、载入、切镜及历史重置，再启用跨帧流水。
4. 分别记录游戏帧周期、NR 网络时间、转换合成时间和 CPU 等待；进行同场景 A/B。
5. 接入面板、启动器和恢复流程，最后增加另一后端的切换。

当前安装器检查 `Cyberpunk2077.exe` 与 OptiScaler 配置。新游戏需修改目标检查、启动命令、加载方式和对应渲染挂钩；不要靠重命名 EXE 复用安装器。D3D11 或原生 Vulkan 游戏应实现相应资源与同步适配，不沿用 D3D12 状态转换。

## 5. 模型与硬件边界

当前发布针对 Windows x64、Intel Arc B580、可变NR输入与固定协同矩阵配方。其他 GPU 的 subgroup、矩阵片段布局、SLM 和寄存器预算应单独适配。使用nr_geometry.h和nr_network_shape.inc生成各层几何、buffer、PUSH及grid；分辨率变化时先退休在用资源并重置历史。Full540的补边初始化和低分辨率残差输入域处理必须一起保留。

保留许可和望月 DLSSNR-AMD 致谢；模型容器可以由安装脚本下载并在本机提取；FSR XMX由使用者单独准备，第三方组件不随源码重新分发。

## English integration checklist

Reuse the Vulkan network and packing recipes; implement the target game's capture/composite adapter. Match input geometry, texture formats, exposure/color space and frame identity. Preserve producer/consumer fences and retire shared resources before reuse. Reset history on scene/resource/model changes. Validate passthrough, fixed frames, motion, resets, streaming and backend switching in that order. Measure network time and game period separately. Replace Cyberpunk-specific executable checks, launch commands and hooks for the target engine.
