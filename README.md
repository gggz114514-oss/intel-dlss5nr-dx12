# Intel DLSS5 NR — DX12 Universal

Vulkan NR Full and Small for Intel Arc, integrated into OptiScaler. The current pre-release is **v0.7.2** and includes FSR XMX and frame generation dependencies.

[Download v0.7.2 ZIP](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/download/archive-v0.7.2-controls/Intel-NR-Unified-v0.7.2-controls.zip) · [Release notes](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/tag/archive-v0.7.2-controls) · [中文说明](README.zh-CN.md)

## Install and uninstall

1. Fully extract the ZIP and exit the game.
2. Open **Setup.cmd** and select the game's DX12 rendering executable. Enhanced ASI is the default. Built-in spoofing is also available with the same host and models.
3. Select **Inspect & Install** and review the confirmation. For removal, use **Inspect & Uninstall** in the same window. The completion dialog shows the backup and log folder under `intel-nr-backups`.
4. Launch the game, enable upscaling and press **Insert** for the Intel NR panel.

Windows 10/11 x64 and a compatible Intel GPU driver are required. The measured platform is Arc B580. [Full installation guide](INSTALL.md)

| Game | Rendering executable |
| --- | --- |
| Where Winds Meet CN | `Engine/Binaries/Win64rh/yysls.exe` |
| Cyberpunk 2077 | `bin/x64/Cyberpunk2077.exe` |
| The Witcher 3 next-gen | `bin/x64_dx12/witcher3.exe` |

These three games were tested with the earlier host and routing. Other DX12 games can be selected. The v0.7.2 host build and installer checks passed. In-game verification of the updated controls is pending.

## NR controls

Choose Full or Small and native, 720p, 540p, 480p or 360p NR processing. The default is Full, native and Classic. Enable **Residual extrapolation**, enter a multiplier from **1 to 4** and press Enter. Fresh settings prefill 2 and start with extrapolation off. Save settings to retain both values.

The installer includes the matched host, Vulkan runtime, model assets and SR/FG dependencies. [Third-party notices](LICENSES.md)

## Versions and source

The archive contains each version's installer, matching source materials and verification status. The repository's root `Install-NR.cmd` uses the pinned v0.4.0 package.

[Iteration archive](docs/ITERATION-ARCHIVE.md) · [Source snapshots](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/tree/archive/iterations-20261107) · [v0.4.0 release](https://github.com/gggz114514-oss/intel-dlss5nr-dx12/releases/tag/v0.4.0-dx12)

## Special thanks: two AMD community projects

- **[mochizuki0323 / DLSSNR-AMD (望月)](https://github.com/mochizuki0323/DLSSNR-AMD)**: model unpacking, network organization, producer/consumer layouts and input-processing reference for the Vulkan backend.
- **[TheAutomatic / dlss-5-amd-project](https://github.com/TheAutomatic/dlss-5-amd-project)**: the OptiScaler host, game input contracts, submission/resource lifetime and panel integration used as the base of this universal bridge.

Their licenses remain distinct: MIT for the Mochizuki source, GPL-3.0 for the derived OPT host, and each dependency’s own terms. This is a community project, with no AMD/Intel/NVIDIA endorsement. [Attribution](docs/optimization/AMD-ATTRIBUTION.md).

## The Witcher 3 comparison

User-confirmed order: **Small → NR off → Full**, with **frame generation disabled in all three**. Original PNGs are preserved without resizing or color processing. Compare skin, hair and armor; the instantaneous FPS overlay is not a paired benchmark.

### Small

![Small, frame generation off](docs/images/witcher3-small.png)

### NR off

![NR off, frame generation off](docs/images/witcher3-nr-off.png)

### Full

![Full, frame generation off](docs/images/witcher3-full.png)

## Performance history

Arc B580, 1280×720 NR input:

| Measurement | Result |
|---|---|
| Full historical starting point → optimized head | **50.821 → 17.450 ms** |
| Latest Full paired campaign | **18.724 → 17.450 ms** |
| Optimized Small paired batch | **14.324 → 9.980 ms** |

These are historical NR inference measurements. Input resolution and workload affect timings. [Full optimization history](docs/optimization/FULL-HISTORY.md).

## Development and diagnostics

[Source/build instructions](docs/DX12-DEVELOPMENT.md) · [Small GPU-hang investigation](docs/optimization/SMALL-GPU-HANG-20261103.md) · [Package identities](docs/releases/20261103-package-manifest.json) · [Other-game integration](PORTING.md) · [Experiment tools](tools/experiments/README.md) · [Recovery](docs/RECOVERY.md) · [Index](docs/INDEX.md)

Run **Collect-Logs.cmd** to export diagnostics locally. Review the archive before sharing it; include GPU/driver, game, model, NR resolution and reproduction steps. No automatic upload, saves or captured frames are included.
