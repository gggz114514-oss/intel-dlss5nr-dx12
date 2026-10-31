# 当前构建与验证 — 2026-11-01

当前版本为v0.3.0-multires。推荐从根目录 `Install-Cyberpunk.cmd` 进入；手动构建继续使用 `tools/Build.ps1`，默认 `vk102` 为Full232派发，`small`为新版Small108派发。

先构建Full，再给Small传 `-FullModel <Full模型目录>`。这份共享末端blend参数沿用已采纳Small，其他权重来自Small自己的DLL。两个模型分别使用build/vulkan-vk102和build/vulkan-small，组包按profile选择DLL；桥使用build/bridge。输出目录支持绝对路径和相对路径。

模型下载/身份名单在tools/fetch_install_models.py和model_profiles.json。Full原版E16和原社区8270两个精确SHA对应的599张量、165冷资源完全一致；Small保留2bc1精确SHA、255张量和70冷资源。提取DLL作为数据，不执行它。

每模型39份网络shader遵守固定glslang16.5.0及NoContraction配方；六份桥shader覆盖原生gather/scatter与低分辨率gather/scatter。720p计划作为种子，nr_geometry.h和nr_network_shape.inc按R/N生成实际buffer、PUSH、grid与padding。

本轮实际完成两模型公开源码全流程构建：后端5/5、桥11/11、面板与组包初始化均通过。GPU验证包括两模型22组原生尺寸；修复设备函数缓存后，两模型共6次生命周期、42帧逐字节等于720p基线。Full540padding与低分辨率强度0回归另已通过。完整口径见[当前构建验收](../../BUILD-VERIFICATION.md)。

publication-validation.json、new-user-validation.json等2026-10月记录保留原始版本身份；它们是历史证据，不是本轮验收文件。性能继续区分网络推理、准备/恢复、桥内重叠等待与游戏帧周期；不得相加不同批次收益。低分辨率早期计时与最终画质门分别列于[多分辨率说明](../MULTIRES-20261101.md)。
