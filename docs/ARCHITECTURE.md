# Architecture

Shipyard owns reusable Xbox platform services and compiler projections. A game
owns its lifecycle, data formats, save policy, upstream version, and adapter
interfaces. Generated sources are outputs; fixes belong in their projector or
maintained source.

## Package layout

- `sources/`: native services and compatibility headers.
- `python/xbox360_platform/`: source transformations and package checks.
- `tests/`: Python tests, host C++ harnesses, and PowerPC compile checks.
- `third_party/` and `provenance/`: pinned inputs and their source notices.

`baseline.json` hashes every file under `python/`, `sources/`, and `third_party/`.
Consumers verify that manifest before generating or compiling sources.

## Service boundaries

| Shared component | Input from the game | Policy owned by the game |
| --- | --- | --- |
| Xenos renderer, framebuffers, shader cache, and shader lowering | Xbox SDK, pinned Fast3D types, `shader_provider.h`, `services.h`, and `xenos_backend.h` | Device lifecycle, feature decoding, logging, failure handling, and optional shader-cache location |
| ImGui renderer and XInput | Xbox SDK, pinned ImGui types, and the functions in `shipyard_imgui_xenos.h` | GUI lifecycle, menus, and input mappings |
| XAudio2 sink and PCM pool | Audio channel definitions and producer callbacks | Channel layouts and producer lifetime |
| Diagnostics queue | Synchronization APIs, sink callbacks, and caller context | Formatting, worker placement, and shutdown timing |
| Storage, environment, network, and atomic file operations | Title identity, mount names, paths, and sockets | Serialization, lifetime, and failure handling |
| ROM intake: discovery, byte order, CRC32C, and SHA-1 | ROM folder and accepted sizes | Version tables, header fixes, archive selection, and extraction |
| Async tasks, thread pool, clock, and libultra timer | Pinned thread-pool input and Xbox synchronization APIs | Task admission, ownership, and shutdown order |
| JSON, libzip, CRT, value types, and compiler projections | Locked upstream sources and parser dependencies | Version adaptation, serializers, archives, and generated-file staging |
| Fast3D math, caches, and vertex helpers | Pinned interpreter types and source anchors | Layout, capacity, formulas, and observation points |

Shared JSON vector conversion reserves destination storage before copying
elements. It preserves element order, type checks, and allocation-failure order.
Normal-batch helpers leave empty output untouched; a singleton uses its own
normal without populating the lookup table.

## Renderer capabilities

Adapters choose the following definitions when compiling `sources/xenos/core.cpp`.
All names below have the prefix `SHIPYARD_XENOS_`.

| Definition | Default / Warwing | 3Shipsty | 2Ship3Shipsty |
| --- | --- | --- | --- |
| `SHADER_ABI` | 3 | 2 | 3 |
| `SHADER_PACK` | 0 | 1 | 0 |
| `DEFERRED_FRAMEBUFFERS` | 0 | 1 | 0 |
| `BINDING_REUSE` | 0 | 1 | 0 |
| `TILING_BUDGET` | 0 | 1 | 0 |
| `DIAGNOSTICS` | 1 | 0, or 1 for instrumented builds | 1 |
| `DETAILED_TIMING` | 1 | 1 | 0 |
| `ALPHA_HISTOGRAM` | 0 | 1, active in instrumented builds | 0 |
| `TILING_TRIALS` | 1 | 1 | 0 |

Shader ABI 3 supplies primitive depth and logical raster state. ABI 2 uses the
shader-pack format selected by 3Shipsty. Runtime shader lowering and pack
generation must pass the same ABI to `Shipyard_LowerShader`; its default is 3.
A pack consumer supplies `Shipyard_DecodeFast3DFeatures` and a program callback.

Deferred framebuffers preserve published depth, defer clears, and keep resolved
images until a draw. HiZ is enabled only with fresh depth. Binding reuse avoids
repeating upload bindings, filtering changes, and noise-scale updates. Tiling
budget selects the reservation policy; tiling trials remain a separate option.

Renderer counters live in `sources/xenos/metrics.h`. Warwing reads and resets
them with `ShipyardXenos::CaptureAndResetCounters`. 3Shipsty can compile them out.
Native-state checks are opt-in through `Shipyard_VerifyFast3DReuse`; Warwing uses
`SMASH360_VERIFY_NATIVE_REUSE=1`, and 3Shipsty defaults to disabled.

## Projector options

Opcode lowering accepts `composed_dispatch=True`. Resource lookup supports the
selected load signatures, optional owned-rvalue construction, and task draining.
Logging generation accepts `normalize_file_paths=False` when the adapter retains
its path handling. Version choices stay explicit in the adapter.
