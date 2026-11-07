# 近期迭代归档

**[v0.7.3](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/tag/v0.7.3)** · B580 型号限制修复，B570 已验证。Release 提供安装包及对应源码。 / Fixed the B580-only restriction, verified on B570. The release includes the installer and matching source.

归档范围为 2026-11-03 至 2026-11-07 的 12 个带独立版本标识的安装包。Release 标记为预发行版。

| 版本 | 变更 | 验证状态 |
| --- | --- | --- |
| [v0.7.2-controls](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/tag/archive-v0.7.2-controls) | 外推改为默认关闭的独立开关。勾选后可输入 1–4 倍，支持小数，按回车应用。新配置预填 2 倍。 | 宿主构建、两种方式安装卸载与 ZIP 校验通过。游戏内输入交互待复验。 |
| [v0.7.1-controls](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/tag/archive-v0.7.1-controls) | 修正 Tone 与肤质控制，分离最终混合和残差外推，移除同帧合成按钮。外推滑块范围 1–2。 | 宿主与四个着色器入口构建通过。两种接入方式安装、切换与卸载通过。新版画面待实机复验。 |
| [v0.7.0-fp16-next-offline1](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/tag/archive-v0.7.0-fp16-next-offline1) | 改为一套最新宿主与 Full／Small，提供增强 ASI 和内置伪装两种接入方式。集成新 Full 与 Small。 | Full 旧 CRT 兼容构建的五组 35 帧与封存版逐位一致。Small 保持封存 DLL。两种方式安装与卸载通过，新模型待游戏内复验。 |
| [v0.6.1-small-color-offline1](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/tag/archive-v0.6.1-small-color-offline1) | 统一安装器的推荐载荷加入通过离线验收的修色 Small。 | 7,372,800 个输出元素零超差。完整包切换与卸载通过，游戏内观感待复验。 |
| [v0.6.0-unified-local2](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/tag/archive-v0.6.0-unified-local2) | 修复双击启动时安装包根目录为空，保留双语界面与卸载确认。 | 通过默认根目录安装和卸载回归。 |
| [v0.6.0-unified-local1](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/tag/archive-v0.6.0-unified-local1) | 整合双语检查、安装和卸载。历史版本提供两套完整载荷选择。 | 隔离环境安装与卸载通过。Windows PowerShell 5.1 的双击默认路径问题在 local2 修复。 |
| [v0.5.1-dx12-small-color-offline1](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/tag/archive-v0.5.1-dx12-small-color-offline1) | 替换经过权重布局、残差偏移与历史系数修正的 Small。 | Small 通过离线数值与媒体检查。游戏内画面待复验。 |
| [v0.5.0-dx12-yysls1](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/tag/archive-v0.5.0-dx12-yysls1) | 加入燕云国服接入修正、NR 同步修正和 DLSS-G → XeFG 帧号映射。 | 燕云国服完整卸载重装后，用户确认超分、NR 与 NVIDIA 插帧正常。赛博朋克 2077 和巫师三 DX12 随后验证了同一接入路线。 |
| [v0.5.0-dx12-test1](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/tag/archive-v0.5.0-dx12-test1) | Intel 专用宿主和通用 DX12 安装的早期本地候选。包内 FFX 查询识别 27 个超分提供项。 | 完整包安装和卸载通过。该版记录中的宿主全量 CI 尚有失败，燕云游戏验收留待后续版本。 |
| [Intel-NR-20261103-style-only-small-align4k](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/tag/archive-Intel-NR-20261103-style-only-small-align4k) | 早期 Style 调整和 Small 4 KiB 对齐版。独立安装、卸载入口。 | 历史候选。仅保留原包内安装脚本与着色器源码。 |
| [Intel-NR-20261103-r3-natural-fix-small-align4k-nvidia-xefg](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/tag/archive-Intel-NR-20261103-r3-natural-fix-small-align4k-nvidia-xefg) | 保留 Natural 调整和 Small 对齐，集成 NVIDIA 伪装与 XeFG 配置。 | 历史候选。仅保留原包内安装脚本与着色器源码。 |
| [Intel-NR-20261103-natural-fix-small-align4k](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/tag/archive-Intel-NR-20261103-natural-fix-small-align4k) | 在早期对齐版上调整 Natural 色调。 | 历史候选。仅保留原包内安装脚本与着色器源码。 |

各版完整更新说明、源码快照与安装包哈希均可在归档分支查阅。
[源码归档](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/tree/archive/iterations-20261107)

v0.7.2 的用法见 [安装与卸载](../INSTALL.md)。
