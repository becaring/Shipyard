# Shared package capabilities

API 7 has one package root and one baseline. Each consumer keeps an independent
commit pin. A version adapter chooses capabilities explicitly; the package does
not inspect a game name to choose behavior.

## Xenos

`sources/xenos/configuration.h` documents compile defaults. Define capabilities
when compiling `core.cpp`; the consumer's generated manifest records them.

| Capability suffix (`SHIPYARD_XENOS_`) | Default / Warwing | OoT | Majora |
| --- | --- | --- | --- |
| `SHADER_ABI` | 3 | 2 | 3 |
| `SHADER_PACK` | 0 | 1 | 0 |
| `DEFERRED_FRAMEBUFFERS` | 0 | 1 | 0 |
| `BINDING_REUSE` | 0 | 1 | 0 |
| `TILING_BUDGET` | 0 | 1 | 0 |
| `DIAGNOSTICS` | 1 | 0 (1 for instrumented builds) | 1 |
| `DETAILED_TIMING` | 1 | 1 | 0 |
| `ALPHA_HISTOGRAM` | 0 | 1 (instrumented builds only) | 0 |
| `TILING_TRIALS` | 1 | 1 | 0 |

ABI 3 supplies primitive depth and logical raster/depth state. ABI 2 keeps the
existing shader-pack format. `Shipyard_LowerShader` takes an explicit ABI argument
(default 3); runtime and host pack builders must use the same value. A shader pack
consumer provides `Shipyard_DecodeFast3DFeatures` and the optional program callback.

Deferred framebuffers preserve published depth, defer game clears, retain resolved
images until a draw and gate HiZ on fresh depth. Binding reuse avoids redundant
upload bindings, filtering changes and noise-scale updates. Tiling budget enables
the validated native reservation policy. Diagnostics can be compiled out; detailed
timing and experimental tiling are independently selected.

## Source projection

Shared projectors retain explicit version adapters: opcode lowering accepts
`composed_dispatch=True`; resource lookup supports both pinned load signatures,
optional owned rvalue construction and task draining; logging generation accepts
`normalize_file_paths=False` for consumers retaining their existing path boundary.
Other added helpers are available to every consumer without copying the package.

## Validation

Host contracts preprocess the selected native capabilities before extracting real
production methods. They cover both conservative and deferred framebuffer paths,
both shader ABIs, and the exclusion of experimental tiling. Native compilation and
linking of each consumer remains required when adopting a package revision.
Build success does not replace gameplay testing on the target hardware.
