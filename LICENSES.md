# Licenses, provenance and special thanks

## Two AMD community projects that made this work possible

### Mochizuki / 望月 — DLSSNR-AMD

[mochizuki0323/DLSSNR-AMD](https://github.com/mochizuki0323/DLSSNR-AMD), **MIT**, Copyright (c) 2026 mochizuki0323. This is a community project, not an AMD vendor SDK.

The Vulkan backend substantially references its model extraction and unpacking, network structure, attention/FFN/DS/UPS organization, producer-consumer layouts and input processing. The original MIT notice is retained in the root license and `third_party/dlssnr_amd_model_tools/LICENSE`. Tool/profile provenance is in `third_party/dlssnr_amd_model_tools/PROVENANCE.json`; it does not imply every later shader experiment came from the same tool snapshot.

### TheAutomatic — dlss-5-amd-project

[TheAutomatic/dlss-5-amd-project](https://github.com/TheAutomatic/dlss-5-amd-project), **GPL-3.0** host and adapters. The Intel OPT integration is a derivative of this project, pinned to `dd542eb28006a27cba37ba86c37dbd022bdba9a5`.

Its host, menu, input contracts, queue/submission and resource retirement work form the base for the universal bridge. Modified source is in `source/optiscaler-overlay`; the exact unchanged source is restored from the pinned upstream commit. The overlay includes the full GPL and unchanged upstream NOTICE. The upstream chain also credits OptiScaler, Dagherbou, wilsjo2, MatheusGViana and neural-amd-opti; those credits remain intact.

## Scope of the root MIT license

The root MIT license covers the project contributions designated as MIT and retains Mochizuki's copyright. **It does not override GPL-3.0 for the derived OptiScaler host**, other third-party notices, or model/library terms.

The compiled host in the release is accompanied by its GPL source overlay, pinned base and build instructions. The release ZIP includes SOURCE-LICENSES with the project MIT license, upstream GPL, NOTICE and source links; dependency notices are under payload/Licenses.

## Model data and external libraries

NVIDIA model data, Intel/AMD super-resolution and frame-generation libraries, FSR XMX, Microsoft runtime/compiler components and other dependencies retain their respective licenses. The project MIT license does not relicense these binaries or grant model rights. The complete user installation bundle is a Release asset; model data and compiled libraries are not committed to Git history. Keep all supplied notices when repackaging and consult the applicable upstream terms for each component.

Khronos headers/glslang and the build SDKs remain external development dependencies. Microsoft VC++ prerequisites are downloaded from Microsoft and signature-verified when needed.

## Screenshots

The ten user-supplied PNGs in docs/images are documentation illustrations. Game and third-party visual rights remain with their owners. Captured inputs, private logs, GPU dumps and local test outputs are not published in the source repository.

No vendor endorsement is asserted. [Detailed migration boundaries](docs/optimization/AMD-ATTRIBUTION.md).
