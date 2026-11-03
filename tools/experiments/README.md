# 实验工具：构建、回放、数值、性能与恢复

当前用户发行版为 v0.4.0-dx12。本页为开发者的 Vulkan 实验工具；使用此前保留的多分辨率组件及 runner 接口。组件构建目录为 build/vulkan-vk102 与 build/vulkan-small，实验目录与它们分开。原生/低分辨率组件测试与离线固定计划runner是两种入口，不混用输入合同。

## 新增：多分辨率组件验证

- `tools/prepare_multires_inputs.py --width 960 --height 540 --out local/experiments/input-540`：生成FP32颜色、原生像素单位MV、深度和哈希，仅CPU。
- 在对应模型构建目录执行 `cmake --build build/vulkan-vk102 --config Release --target nr_multires_component_gpu`；先用 `--help`查看接口，不运行GPU。
- 组件宿主参数为 `DLL MANIFEST INPUT_ASSETS FRESH_OUT R_WIDTH R_HEIGHT O_WIDTH O_HEIGHT NR_HEIGHT [baseline]`。NR_HEIGHT=0为原生；不带baseline时包含切档及返回检查，带baseline用于同尺寸七帧回归。
- 正式执行前关闭游戏、按下文的独占锁协议提交具名GPU任务；`Run-Gpu.ps1`封装的是计划runner，不直接拿它的参数调用组件宿主。
- `tools/compare_multires_frames.py --baseline <父输出目录> --candidate <候选输出目录> --receipt <结果.json>`：逐字节核对七帧，仅用于同输入720路径回归。不同处理分辨率需单独做数值/画面比较，不要求逐位相同。
- 合成输入用于尺寸、反馈、重置和强度0检查；画质使用真实输入并明确MV/depth/history条件。计时分别记录网络、准备/恢复和游戏周期。


这些工具用于恢复本项目的实验能力。默认游戏构建不执行 GPU 实验。实验输出放在 `local/experiments/`，模型与采集数据保持本地。

## 工具表

| 工具 | 用途 | 是否运行 GPU |
|---|---|---|
| `port_network_model_runner` | 离线派发、反馈、逐派发时间戳、中间张量 dump | 普通运行是；`--help` / `--cpu-check` 否 |
| `Run-Gpu.ps1` | 独占文件锁、串行执行、回执及输出哈希门 | 是 |
| `make_capture_replay.py` | 用采集元数据生成独立帧回放计划 | 否 |
| `compare_frames.py` | FP32 输出对比，RGB 偏差、分区误差、PSNR | 否 |
| `analyze_timing.py` | 保留全部样本的配对进程组 t 区间统计 | 否 |
| `mma_operand_audit.py` | 解析 SPIR-V A/B/C/Result dtype 与 signedness | 否 |
| `restore_research.py` | 展开历史研究源码并替换本地路径占位符 | 否 |
| `inventory_private.py` | 生成待私备数据的大小与 SHA256 清单 | 否；不上传 |

Python 环境使用根目录 `.venv`：

```powershell
.\.venv\Scripts\python.exe -m pip install -r tools/experiments/requirements.txt
```

## 1. 编译离线 runner

先完成对应 Full 或 Small 模型构建。为每个模型使用独立实验构建目录：

```powershell
cmake -S source/vulkan -B build/experiment-full -A x64 `
  -DNR_MODEL_DIR="C:/Dev/intel-dlss5nr-dx12/local/vk102" `
  -DVULKAN_INCLUDE_DIR="C:/VulkanSDK/Include"
cmake --build build/experiment-full --config Release --target port_network_model_runner

build/experiment-full/Release/port_network_model_runner.exe --help
build/experiment-full/Release/port_network_model_runner.exe --cpu-check `
  --plan local/game-package-vk102/manifest/port_network_runtime.txt
```

Small 将目录改成 `build/experiment-small`，`NR_MODEL_DIR` 改为 `local/small`。runner 根据模型构建身份检查行数，Full 为 152，Small 为 76。实验目标为 opt-in，日常 Build.ps1 不自动编译或执行它。

发布的默认计划使用运行时游戏输入，离线零输入可以检查执行，但真实质量实验必须使用采集输入及相匹配的历史状态。不得把零输入结果当作真实场景画质。

## 2. 独占 GPU 执行与数值门

关闭游戏，所有实验共用同一个 `-Lock` 路径。文件句柄独占持有到 runner 退出。记录为 RUNNING 的遗留锁需先核实进程；脚本不自动抢锁。

```powershell
./tools/experiments/Run-Gpu.ps1 `
  -Runner ./build/experiment-full/Release/port_network_model_runner.exe `
  -Plan ./local/experiments/plan-fixed.txt `
  -Out ./local/experiments/fixed-base `
  -Lock ./local/GPU.lock -Warm 0 -Reps 1
```

生成目录旁的 `.console.txt`、`.execution.json` 保存参数、runner/plan 哈希、退出与完成信息；runner 目录保存 `port_network_receipt.json` 和计划指定的输出。

精确等价候选增加 `-ExpectedOutputSha256 <参考SHA256>`；若输出名不是 frame_out.bin，增加 `-OutputFile replayed-network.bin`。连续反馈门使用 `-Feedback -SaveFrames -Warm 0 -Reps 2`。父、候选都从相同初态运行，核对每个反馈帧。

`-DumpIntermediates` 用于首差定位，`-PerPassTiming` 用于逐派发画像；它们分别建立诊断任务，不混入 ordinary 总时间采样。

## 3. 从采集数据构建回放

```powershell
python tools/experiments/make_capture_replay.py `
  local/private/source-plan.txt local/private/frame/capture.json `
  local/experiments/replay-frame.txt
```

要求 schema 1 元数据：源计划 SHA256、实际 32-word PUSH、noise_seed、两份 pre/post buffer 快照及输出文件的大小/哈希。工具逐一检查后替换输入，生成新的计划和身份回执，不覆盖已有实验。

这是**带采集历史的独立帧回放**：删除 HISTORY 自动更新以固定输入。连续 INT8 反馈要另建保留 HISTORY 的序列任务，让 INT8 使用自身前帧输出，不把 FP16 历史逐帧灌回后称为序列稳定。

工具先核对采集源计划哈希，再将原计划中的相对权重/shader 路径按源计划目录解析为绝对路径，因此回放计划可以放在独立实验目录。runner 的相对资源路径也以计划所在目录解析。

## 4. 数值与色偏比较

```powershell
python tools/experiments/compare_frames.py `
  local/experiments/fp16/replayed-network.bin `
  local/experiments/int8/replayed-network.bin `
  --shape 2 720 1280 4 --view 0 --peak 1 `
  --out local/experiments/quality.json
```

输入为 little-endian FP32。默认只比较第一 view 的 RGB，保留实际数值，不做调色、截断或归一化补偿。报告包含 bit equality、RMSE、PSNR、各通道有符号平均偏差、MAE、最大误差、P99，以及 4×4 区域偏差。完全相同时 PSNR 字段为 null，bit_equal 为 true。

色偏定位先核输入与帧身份，再寻找第一个出现系统性偏差的中间张量。逐模块比较整数参考和父 FP16：整数参考不符先修数学/布局，参考一致再分析量化误差。PSNR 和画面、RGB 偏差、反馈序列共同记录。

## 5. ordinary 配对计时

通过数值门后，父/候选采用 `-Warm 1 -Reps 30 -Feedback`，每个进程使用新目录。多组交替先后顺序，保留全部样本。任务清单每个 job 包含：

```json
{
  "jobs": [{
    "id": "pair0-base", "candidate": "base", "pair": 0,
    "runner_output": "local/experiments/pair0-base",
    "plan": {"path": "local/experiments/base.txt", "sha256": "实际计划SHA256"},
    "expected_frame_out": "与本次30帧协议对应的末帧SHA256"
  }]
}
```

为每组填入 base/cand 两条记录，使用 2–12 组。分析 spec：

```json
{
  "manifest": "local/experiments/jobs.json",
  "comparisons": [{"arms": ["base", "cand"]}],
  "extra": {"passes": 232, "reps": 30},
  "out": "local/experiments/timing.json"
}
```

```powershell
python tools/experiments/analyze_timing.py local/experiments/spec.json
```

统计单位是进程组均值差，使用组数−1自由度的双侧 t 95% CI。正值表示候选更快。Small 的 passes 改为 108。所有路径相对当前工作目录；保留 samples、原始回执、计划身份和输出哈希。删除帧文件后仍可用同目录 FRAME-HASHES.json 中 `frame_out.bin.sha256` 保留验证链。

## 6. SPIR-V 与 native

```powershell
python tools/experiments/mma_operand_audit.py --json local/experiments/mma.json `
  local/vk102/assets/spv/实际shader文件.spv
```

该工具读实际 A/B 操作数类型，不用结果为 f32 推断输入为 FP16。每个候选同时保存 shader 哈希与派发位置。

GTPin、GPU 驱动与厂商分析工具另行安装。自定义 profiler、native 解析和依赖图脚本保存在研究档案的 phase-diagnosis、zcode-combo1-profile、fullnet-continuous 等目录。按其 source/CMake 和本机 GTPin SDK 构建，先读取该安装版本的 help，再运行具名采集任务。保留工具版本、插桩范围、输出身份和普通运行对照。mode0 是基本块口径；密集插桩屏障数据不当作原核逐指令等待。

## 7. 来源

回放工具来自实际采集工作流；计时统计移植自 fullnet-continuous 的 ANALYZE.py，移除了工作站依赖；MMA 操作数审计保留原研究实现。原版本与其他候选一并归档。恢复全项目见 [恢复手册](../../docs/RECOVERY.md)。
