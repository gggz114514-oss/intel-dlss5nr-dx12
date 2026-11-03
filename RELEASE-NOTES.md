# v0.4.0 — Intel NR DX12 Universal

## Download / 下载

Download **Intel-NR-DX12-20261103.zip**, extract everything, run **Install.cmd**, select your game's EXE. No Python, Visual Studio, Vulkan SDK or preinstalled OPT required. Source users can run **Install-NR.cmd** to download and hash-check this same bundle.

下载完整安装包并解压，运行 Install.cmd。支持安装入口：赛博朋克 2077、巫师三 DX12、死亡搁浅导演剪辑版。卸载用 Uninstall.cmd，日志用 Collect-Logs.cmd。请保留安装记录和备份，勿手动删除游戏原生 FidelityFX DLL。

## Changes

- Optimized Full and Small with variable native resolution and 720/540/480/360p processing choices.
- OptiScaler integrated menu and universal DX12 input bridge; matched host/runtime/shader.
- Small 4–64 KiB buffer alignment stability fix; complete investigation published.
- Natural tone LUT correction and continuous style strength; Classic/Natural/Cinematic retained.
- Full/native/Classic defaults; per-process NVIDIA DXGI/Streamline spoofing + XeFG, with Death Stranding's upscaler FG input handled by the installer. Existing preferences preserved.
- Verified backups, native FidelityFX restoration on uninstall, VC++ prerequisite handling and log export.
- Three original Witcher screenshots on the homepage, Small / NR off / Full, all without FG.

## Special thanks / 特别鸣谢

**mochizuki0323 / DLSSNR-AMD (望月)** — MIT, network organization and producer-consumer layouts. **TheAutomatic / dlss-5-amd-project** — GPL-3.0, OptiScaler host and universal game integration. Full notices and source links are retained in the ZIP and repository.

## Validation scope

This release packages the already game-tested DLLs, shader and model assets; it does not replace them with untested rebuilds. Package hashes, installer tests and source checks are documented in the repository. Full ~17.45 ms and Small ~9.98 ms are historical B580/720p network timings; the private Cyberpunk streaming −5.43 ms is a separate historical game-period result.

See INSTALL.md for use, LICENSES.md for licenses and docs/optimization/SMALL-GPU-HANG-20261103.md for stability evidence and costs.
