# 换电脑后的项目恢复手册

恢复分为两部分：**公开 GitHub 保存代码与研究方法；私人存储保存模型、场景输入和原始结果。** 公开源码可恢复新版FP16构建、可变分辨率、安装脚本和实验工具。可重新下载或生成的数据不要求重复上传；私人备份优先保留不可重建的成果。

## GitHub 保存什么

| 内容 | 位置 |
|---|---|
| 当前 Full / Small 源码与模型配方 | source/network-fp16-20261027、source/network-small-20261028、tools |
| 游戏桥、流式交接、控制面板 | source/bridge、source/vulkan、panel |
| 可直接使用的实验工具与说明 | tools/experiments |
| 优化历程、最终收益与验证摘要 | docs/optimization |
| 历史研究源码、候选、计划、任务与报告 | source/research-archive |
| 档案文件身份与原始位置 | source/research-archive/INDEX.json |

档案保留 5,699 个研究文件，以 5,013 个内容寻址对象去重存储；INDEX 将每个对象映射回原目录。研究档案覆盖主审计工作目录、DSH INT8 实施与细量化、XMX 验证包、zcode 诊断目录及早期报告。每个文件保留原始 SHA256 和发布版 SHA256。路径匿名化之后发布字节不同，原始身份用于与私人备份核对。

历史 Python/PowerShell 以 `.py.txt` / `.ps1.txt` 保存，防止误点执行挂载、清理或 GPU 脚本。它们是历史实现档案，不参与默认构建，也不自动成为当前采纳版本。当前版本以 tools/model_profiles.json 及源码构建配方为准。

## 恢复步骤

1. 从v0.3.0-multires或main克隆仓库，保存实际commit ID。
2. 安装 INSTALL.md 所列构建依赖；安装实验 Python requirements。
3. 使用Install-Cyberpunk.cmd下载并校验固定模型，自动先构建Full再构建Small；已有模型缓存可用-FullDll/-SmallDll。
4. 安装器按实际游戏路径生成默认Full/原生/流式配置，保存INSTALL-RECEIPT.json与逐文件备份；恢复旧配置时使用对应收据。
5. 如需继续实验，构建各模型独立的 port_network_model_runner，先执行 CPU plan check。
6. 恢复采集输入、历史、输出、原始计时回执和候选权重，按 SHA256 清单核对。
7. 展开历史研究档案，将占位符指向新电脑路径；只执行本次选定任务，历史挂载/清理脚本先人工核对。
8. GPU 任务统一使用一个锁文件，先重建固定输入参考，再做候选数值门、反馈和计时。

## 展开研究档案

在本地创建 `local/recovery-paths.json`，不要提交个人路径配置：

```json
{
  "@NR_WORKSPACE@": "C:/NR/research",
  "@NR_GPU_WORKSPACE@": "C:/NR/gpu",
  "@NR_GAME@": "C:/Games/Cyberpunk2077",
  "@NR_USER_HOME@": "C:/NR/private-home-restore",
  "@NR_AGENT_DATA@": "C:/NR/agent-data",
  "@NR_LEGACY_WORKSPACE@": "C:/NR/legacy"
}
```

```powershell
python tools/experiments/restore_research.py `
  --paths local/recovery-paths.json --out C:/NR/research
```

输出目录必须尚不存在。工具检查档案哈希，恢复原文件相对路径和脚本扩展名，替换已登记的根路径，不执行任何恢复出的脚本。历史脚本中的其他工具安装路径、外部模型/捕获文件按其任务说明重新配置；INDEX 记录明确排除了生成目录与未纳入的记录。

## 私人备份取舍

| 优先级 | 保存内容 | 原因 |
|---|---|---|
| P0 | 尚未进入Git的原创源码、明确采纳的改动、任务状态与恢复索引 | 不能靠重新下载恢复 |
| P0 | INT8已接受尺度、标定方案和关键结论 | 保留尚未公开的研究成果 |
| P1 | 当前安装收据、配置与回滚备份 | 恢复实际挂载与用户设置 |
| P1 | 关键验收结果和最小复现输入 | 复查已接受结论 |
| P2 | 不再能从上游获取的模型/工具版本 | 按需保留，使用私人存储 |

能按脚本重新下载的模型、编译产物、环境、完整GPU dump以及能重新采集的场景，不默认上传。24帧采集也不是恢复源码或安装新版的必要条件；继续某次严格同输入实验时再保存对应最小数据集。不要把整个工作区或所有run目录当成必须上传的备份。

模型和游戏文件遵守各自条款；凭据不进入公共仓库。

## 建立私人备份清单

先将需要保存的数据整理到私人备份目录，再生成清单：

```powershell
python tools/experiments/inventory_private.py `
  --root C:/NR/private-backup --out C:/NR/private-backup-index.json
```

此工具仅扫描并生成 SHA256，不上传、不复制数据。将数据目录与清单一起备份到用户选定的私人存储，完成后下载一份到新目录重新生成清单并比较路径、大小和哈希。私人存储尚未建立时，原始数据仍只存在本机；公开源码备份不能代替这一步。

## 研究档案的取舍

保留自己的研究源码、候选 shader、计划、关键状态及文档。依赖虚拟环境、厂商工具二进制、编译产物、模型数据、游戏帧、原始 run/evidence 目录不进入公开档案。构建生成物从源码复建，实验原始数据走私人备份。未选中的大文本及路径项在 INDEX 中列明，便于私人备份补齐。
