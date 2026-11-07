# 换电脑后的项目恢复手册

恢复 2026-11-03 至 11-07 的本地迭代时，从 [版本索引](ITERATION-ARCHIVE.md) 下载同版安装包和源码附件。各版的 SHA-256、说明文件与源码范围在 Release 和归档分支可查。

## 原项目恢复流程

恢复分为两部分：**公开 GitHub 保存代码与研究方法；Release 保存完整用户包；私人存储保存未公开研究、最小复现输入和原始结果。** 公开源码可恢复新版FP16构建、可变分辨率、安装脚本和实验工具。可重新下载或生成的数据不要求重复上传；私人备份优先保留不可重建的成果。

## 当前 DX12 通用版恢复

完整用户包在 v0.4.0-dx12 Release；源码入口 Install-NR.cmd 下载并校验该包。通用宿主的固定上游、全部修改与恢复步骤见 [DX12-DEVELOPMENT](DX12-DEVELOPMENT.md)。历史私有桥继续保留。实验采集与私人日志仍留在私人备份，不混入安装包。

## GitHub 保存什么

| 内容 | 位置 |
|---|---|
| 当前 Full / Small 源码与模型配方 | source/network-fp16-20261027、source/network-small-20261028、tools |
| 当前 OPT 通用桥、Vulkan 组件与游戏内面板 | source/optiscaler-overlay、source/vulkan |
| 历史私有桥与网页面板 | source/bridge、panel |
| 可直接使用的实验工具与说明 | tools/experiments |
| 优化历程、最终收益与验证摘要 | docs/optimization |
| 历史研究源码、候选、计划、任务与报告 | source/research-archive |
| 档案文件身份与原始位置 | source/research-archive/INDEX.json |

档案保留 5,699 个研究文件，以 5,013 个内容寻址对象去重存储；INDEX 将每个对象映射回原目录。研究档案覆盖主审计工作目录、DSH INT8 实施与细量化、XMX 验证包、zcode 诊断目录及早期报告。每个文件保留原始 SHA256 和发布版 SHA256。路径匿名化之后发布字节不同，原始身份用于与私人备份核对。

历史 Python/PowerShell 以 `.py.txt` / `.ps1.txt` 保存，防止误点执行挂载、清理或 GPU 脚本。它们是历史实现档案，不参与默认构建，也不自动成为当前采纳版本。用户安装版本以 tools/intel-nr/PACKAGE.json、Release 完整包及其文件清单为准；模型构建配方由 tools/model_profiles.json 记录。

## 恢复日常使用

1. 从 v0.4.0-dx12 Release 下载完整安装 ZIP，保存 ZIP 与 SHA256SUMS.txt。
2. 解压并双击 Install.cmd，选择游戏 EXE；无需准备开发工具。也可从整个仓库运行 Install-NR.cmd，自动下载并校验相同包。
3. 安装默认 Full／原生／经典，在 OPT 游戏内面板调整。已有 INI 优先保留。
4. 保存游戏目录的 intel-nr-install.json 与 intel-nr-backups。卸载用 Uninstall.cmd，恢复被覆盖的原文件。

## 恢复开发与实验

1. 克隆仓库并保存实际 commit，按 [DX12 源码构建](DX12-DEVELOPMENT.md) 恢复固定上游、Intel 宿主覆盖、Vulkan 组件与模型配方。
2. 为 Full 和 Small 分别构建离线 runner，先执行 CPU plan check。实验 Python 依赖见 [工具说明](../tools/experiments/README.md)。
3. 恢复选定实验的输入、历史、原始回执和候选尺度，按 SHA256 核对；不要把所有可重新生成的数据都当作必须恢复。
4. 展开历史研究档案，重映射路径。历史挂载／清理脚本先审查，不直接用于当前游戏包。
5. GPU 任务统一使用一个锁文件，先重建同输入参考，再做候选数值门、反馈和计时。

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
