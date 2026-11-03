# 当前构建与验证 — v0.4.0-dx12

## 用户安装

完整包提供 Full／Small、OPT 通用宿主和 FSR XMX。解压后运行 Install.cmd；源码目录入口 Install-NR.cmd 下载并校验同一完整包。无需安装开发工具。详见 [安装指南](../../INSTALL.md)。

## 开发构建

[DX12-DEVELOPMENT](../DX12-DEVELOPMENT.md) 是当前构建入口：先恢复固定 TheAutomatic 上游，再应用 Intel GPL 宿主覆盖；Vulkan 组件与模型分别构建。旧 tools/Build.ps1 还会构建历史私有桥，不是新用户安装器。

Full 保留 232 派发，Small 保留 108 派发及自身五个偏移专用核。模型生成先 Full 后 Small，共享末端 blend 参数，不混用两个模型的权重或 manifest。glslang 16.5.0 与 NoContraction 配方、每模型 39 个网络 shader 及六个桥 shader 的身份规则继续保留。

## 当前发布检查

- 完整安装 ZIP 的 392 个 payload 文件 SHA256 通过。
- 安装器专项 5/5；完整 ZIP 经仓库启动器在隔离模拟游戏目录安装／卸载，原生 FidelityFX 文件恢复一致。
- Small Vulkan 组件源码构建通过；网络几何、残差合同、设备派发生命周期三项 CPU 检查通过。
- 37 个 OPT 宿主覆盖文件与固定上游逐一核对，模块合同检查通过。
- 旧 AMD 宿主综合测试在 nr_install_status 用例异常退出，未计为整套通过；细节记录于发布检查清单。
- 本轮发布保留此前实机验证二进制，没有用源码检查产生的重编 DLL 替换安装包，没有新跑 GPU。

[发布检查清单](../releases/20261103-publication-checks.json) · [包身份](../releases/20261103-package-manifest.json) · [Small 挂死处理与成本](SMALL-GPU-HANG-20261103.md)

## 历史证据

[2026-11-01 源码及多分辨率验收](../../BUILD-VERIFICATION.md) 和 [多分辨率专题](../MULTIRES-20261101.md) 保留原测试版本、输入与数值。2026-10 月的 publication-validation、new-user-validation 等仍是对应历史版本的记录，不替代当前安装包检查。性能报告按同批网络区间比较，不累加不同批次收益。
