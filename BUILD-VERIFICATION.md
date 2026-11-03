> 历史记录：以下是 2026-11-01 私有桥／多分辨率源码验收。当前 DX12 通用版见 [发布说明](RELEASE-NOTES.md) 和 [构建入口](docs/DX12-DEVELOPMENT.md)。

# Historical source-build verification — 2026-11-01

Version tested in this historical record: **v0.3.0-multires**. Source builds include native/low-resolution processing, effective panel controls and device-dispatch lifetime fixes.

| Check | Full | Small |
|---|---:|---:|
| Seed-plan rows / buffers / dispatches | 152 / 502 / 232 | 76 / 222 / 108 |
| Network shaders | 39 | 39 |
| Bridge shader variants | 6 | 6 |
| Tensor hashes / cold resources | 599 / 165 | 255 / 70 |
| Backend CPU tests | 5/5 PASS | 5/5 PASS |
| Bridge CPU tests | 11/11 PASS | 11/11 PASS |
| Panel/configuration and actual package initialization | PASS | PASS |
| Native dimension gates, seven frames per case | 11 PASS | 11 PASS |
| Recreated device lifecycles with the DLL kept loaded | 3 × 7 frames PASS | 3 × 7 frames PASS |

The 42 lifecycle frames matched the original 720p outputs byte for byte. Native-size gates cover 640×360 through 3840×2160, including odd dimensions and feedback/reset/mode-return checks. They are functional GPU checks; historical network performance is recorded separately.

## Fresh source build

Actual public `Build.ps1` ran Full without `-Profile` (verifying the Full default), then Small using that Full output. Both completed model extraction, weight packing, shader compilation, native compilation, CPU tests, panel tests and package assembly. Their manifest/runtime plans match the GPU-accepted release identities. Build directories are `build/vulkan-vk102` and `build/vulkan-small`; package assembly explicitly selects the matching component. Assembling Full after Small was also checked.

Generated settings explicitly contain `backend=102` for Full and `input_height=0` for native processing. Model switching and flow-through rendering use the same effective controls, so legacy startup controls cannot replace the selected resolution. Streaming defaults ON; diagnostic readback, color-state proof and generic probes are OFF.

## Model download equivalence

Full accepts the existing `8270b350…` container and original `e16bcf15…` container. Independent extraction verified all 599 tensor hashes, all 165 packed cold resources, the complete WEIGHTS_HT resource and NRMODEL1 payload match. Small retains exact identity `2bc19bc0…`. Full and Small are never selected merely by DLL filename or version text.

## Installer validation

The PowerShell entry was exercised with freshly built packages in an isolated game-directory fixture. Existing OptiScaler, XMX, FG and settings remained byte-identical; NR configuration defaulted to Full/native. A separate clean-directory install exercised the pinned official OptiScaler archive, extraction, ASI loading configuration and file checksums. Restore returned each fixture to its original files. Publication checks exclude binaries, weights, captures and local configuration.

The Small download endpoint and supported local DLL identity were checked; the full 368 MB Small archive was not redownloaded to completion in this release check. Downloads are verified against the pinned archive and DLL hashes before building.

[Installer](docs/ONE-CLICK-INSTALL.zh-CN.md) · [Geometry and quality scope](docs/MULTIRES-20261101.md) · [2026-10-24 historical build record](docs/optimization/BUILD-VERIFICATION-20261024.md)
