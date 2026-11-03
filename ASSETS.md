# Local assets

Model-generation recipe retained from **v0.3.0-multires / 2026-11-01**; current user release is **v0.4.0-dx12**. This page describes current generated assets; historical performance is in [docs/optimization](docs/optimization/README.md).


Build Full first; Small also takes the built Full directory for its accepted shared blend scalar. Model tensors come from the supported `nvngx_dlssnr.dll` or its ZIP, supplied locally or fetched by the pinned installer. Extraction parses PE resources without executing the DLL.

```powershell
python tools/extract_model.py --profile vk102 --dll "C:/Models/Full/nvngx_dlssnr.dll" --out local/vk102
python tools/build_assets.py --model local/vk102 --glslang "C:/Tools/glslang/bin/glslang.exe"

python tools/extract_model.py --profile small --full-model local/vk102 --dll "C:/Models/Small/nvngx_dlssnr.dll" --out local/small
python tools/build_assets.py --model local/small --glslang "C:/Tools/glslang/bin/glslang.exe"
```

Small checks **255** tensor SHA-256 hashes and generates **70** FP16/FP32 model cold resources. VK102 checks **599** tensor hashes and generates **165** model cold resources, including its distinct split16 and final-head layouts. Both pack NRMODEL1. NumPy is needed for B580 packing. `--raw-only` performs standard-library extraction only, not backend preparation. With `--profile auto` (the extractor default), the pinned DLL SHA selects the profile; `Build.ps1` requests it explicitly.

Each profile builds **39** network modules and six bridge shaders (gather, scatter, scatter32, low_gather, low_scatter, low_scatter32) from GLSL, restores/verifies the frozen precision identity, writes its plan (**Small 76 rows / 222 buffers / 108 dispatches; VK102 152 rows / 502 buffers / 232 dispatches**), and generates C++ build identity includes. Shared sources are reused where the deployed shader bytes match; profile-specific sampling and weight-offset variants retain their own included sources. The serialized plan is the 720p seed; the runtime creates effective grids, PUSH, buffer sizes and padding for R/N. Asset paths are relative to the manifest. The built backend checks the manifest, plan and every model/shader file before mounting.

Two old image bundles were developer fixtures, not weights, and are absent. The runtime initializes their buffers to zero, then game gather/controls/history provide frame data. The first pass selects procedural noise from the current frame seed. No developer screenshot, captured activation or pre-exported blob is needed.

`Build.ps1` runs extraction, shader preparation, native builds, CPU checks and package assembly for the selected profile. `local/`, `assets/`, `models/`, `weights/` and `build/` are ignored. Generated weights, SPIR-V, CUDA code and proprietary binaries are not repository contents and are not covered by the source license. The ten explicitly supplied screenshots in `docs/images/` are documentation assets, not runtime fixtures.
