# Licenses and provenance

- Community reference: [mochizuki0323/DLSSNR-AMD](https://github.com/mochizuki0323/DLSSNR-AMD), MIT, copyright 2026 mochizuki0323. It is a community project, not an AMD vendor SDK. Its notice is retained in the root license and `third_party/dlssnr_amd_model_tools/LICENSE`. Model profile commit: `939b6072b792d0f45f8487a0b38a21d31264b6bc`.
- `third_party/dlssnr_amd_model_tools/PROVENANCE.json` records tool/profile hashes and local changes. The tools' MIT license does not grant a license to NVIDIA's model or DLL.
- Vulkan port, bridge, panel and preparation/build contributions are supplied as source under the root MIT notice, subject to retained third-party notices.
- OptiScaler is an external build/runtime dependency, not bundled. Its SDK headers, Detours and other dependencies retain their licenses. No OptiScaler binary or SDK header is copied into this repository. Its GPLv3 source is linked by the installer; original package notices are preserved locally. FSR XMX is excluded from automatic deployment.
- Khronos Vulkan headers and glslang are external development dependencies with their own licenses.
- NVIDIA's DLL/weights/SDK redistributables, Intel/AMD upscaler libraries and game files are excluded. The installer fetches fixed model containers and, when missing, the official OptiScaler release directly to the user’s local cache. Applicable upstream terms remain in force; downloaded files are not uploaded to this repository.

No vendor endorsement or model redistribution right is asserted. The seven user-supplied screenshots in `docs/images/` are published as documentation illustrations, with game and third-party visual rights retained by their respective owners. Runtime fixtures, test outputs, model data, native binaries and compiled shaders are excluded.

## Optimized Vulkan backend attribution (current variable-resolution release)

The current Vulkan backend substantially references mochizuki0323 / DLSSNR-AMD (望月), including network organization and producer/consumer layouts, beyond extraction tools. See [attribution and migration boundaries](docs/optimization/AMD-ATTRIBUTION.md). Both new shader directories retain the upstream MIT notice.
