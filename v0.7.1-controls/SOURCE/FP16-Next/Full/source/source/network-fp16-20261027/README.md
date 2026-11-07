# Accepted optimized FP16 source

Current v0.3.0 uses these 39 Full network recipes as the precision baseline. Runtime geometry lives in source/vulkan/nr_geometry.h and nr_network_shape.inc; bridge resizing does not replace these weights or recipes. Full is the default build and installer model.

See [optimization history](../../docs/optimization/README.md), [AMD attribution](../../docs/optimization/AMD-ATTRIBUTION.md) and [build contract](../../docs/optimization/BUILD-AND-VALIDATION.md).

Recipes restore exact accepted shader identities. Model values and compiled binaries are not distributed. Original upstream MIT notices are preserved in LICENSE.
