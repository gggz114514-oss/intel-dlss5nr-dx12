# Accepted optimized FP16 source

Current v0.3.0 keeps this optimized 108-dispatch Small, its own weights and five offset-specific kernels. Runtime geometry supports native and reduced inputs; build Full first for the accepted shared blend scalar. Build output is build/vulkan-small, distinct from Full.

See [optimization history](../../docs/optimization/README.md), [AMD attribution](../../docs/optimization/AMD-ATTRIBUTION.md) and [build contract](../../docs/optimization/BUILD-AND-VALIDATION.md).

Recipes restore exact accepted shader identities. Model values and compiled binaries are not distributed. Original upstream MIT notices are preserved in LICENSE.
