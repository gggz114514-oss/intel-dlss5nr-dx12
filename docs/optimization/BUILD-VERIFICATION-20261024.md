# Source-package verification

历史构建记录（2026-10-24），不是当前安装指南。新版后端5项CPU、桥11项CPU及多分辨率/生命周期GPU验收见[当前验证](../../BUILD-VERIFICATION.md)。

> 2026-10-29 optimized update: current Full/Small build identities, streaming changes and validation are documented in [docs/optimization](README.md). Older entries below describe the earlier release.


Verified locally on Windows x64, 2026-10-24, with MSVC 14.44.35207, Windows SDK 10.0.26100.0, CMake, Python 3.12 / NumPy and glslang 16.5.0.

Two separate clean directories started with the Git source allowlist, the matching supported user DLL and declared external development dependencies. Neither had **previous model blobs, compiled network shaders or game samples**. VK102 did not consume an existing VK102 package; it reconstructed model assets from the full-model DLL. Comparisons to prior assets were performed afterward, outside the build.

| Check | Small | VK102 |
|---|---|---|
| DLL extraction and expected tensor hashes | 255/255 | 599/599 |
| B580 model resources, byte hashes vs existing backend | 70/70 | 150/150 |
| Network shaders rebuilt from included source to frozen hash | 40/40 | 40/40 |
| Bridge shaders built from source | 3/3 | 3/3 |
| Runtime rows / buffers / dispatches | 76 / 223 / 118 | 152 / 487 / 266 |
| Vulkan backend/component build | PASS | PASS |
| Backend recording and native ABI CPU checks | 2/2 PASS | 2/2 PASS |
| ASI build and bridge CPU checks | 11/11 PASS | 11/11 PASS |
| Panel loopback, token rejection, shared INI/state roundtrip | PASS | PASS |
| Local game-package assembly | PASS | PASS |
| Relocated package asset hashes and CPU probe | 113/113 + PASS | 193/193 + PASS |
| Register other model; preserve style and strength | PASS | PASS |

Both end-to-end `Build.ps1` runs completed with exit 0. Full VK102 packing matches all 150 frozen model-resource byte hashes. Its five added source variants compile to the matching deployed shader hashes; the other 35 reuse existing identical recipes. Small's 70 resources and 40 network shaders still match the earlier build.

After normalizing file paths, runtime plans match their previous game baselines. VK102 replaces only two developer image-file inputs with the existing zero-initialized buffer path; BIND, PUSH, POLICY, HISTORY and dispatch geometry remain unchanged. No developer fixture image is required.

Each assembled package was copied to a different directory and passed its CPU asset probe there. Configuration was exercised in both directions (Small boot + VK102, VK102 boot + Small) using a fake game folder. The fake game EXE and OptiScaler INI were unchanged. Boot keys, relocated shared paths, both registered manifests and appearance settings were checked.

Two setup failures are retained in the local validation record: one Vulkan include argument pointed a level above `vulkan/vulkan.h`; one excessively long validation path exceeded the native file API limit. Correcting the include argument and using a shorter directory allowed fresh end-to-end builds to pass. These limits are reflected in the build entry and installation instructions.

These checks made no game, stable-package, driver or global Python changes and submitted no GPU work. The earlier installed baseline had user-confirmed local game smoke testing, including Small/VK102 switching; the new generalized source builds were not installed during this update. The screenshots document the user's working earlier installation. Neither those images nor CPU checks establish extended stability or formal image-quality acceptance. Small reference limitations remain as described in the README.
