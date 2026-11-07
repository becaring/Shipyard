# Architecture

Shipyard owns reusable Xbox platform services and compiler projections. A game
owns its lifecycle, data formats, save policy, upstream version, and adapter
interfaces. Generated sources are outputs; make fixes in their projector or
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
| Storage, environment, network, and atomic file operations. The storage device popup and content mount (`sources/storage/device.h`): `SelectAndMount` calls `XShowDeviceSelectorUI`, presents frames through the game's startup display callbacks while the popup is up, retries once after a sign-in interruption, and mounts with `XContentCreate`. `Flush` and `Close` finish the session | Title identity, mount policy, startup display callbacks, mount names, paths, and sockets | Serialization, lifetime, and failure handling |
| ROM intake: discovery, byte order, CRC32C, and SHA-1 | ROM folder and accepted sizes | Version tables, header fixes, archive selection, and extraction |
| Stored O2R writer: streamed entries, repeat handling, and sorted directory | Entry names and bytes | Archive name, entry naming, and when to finish |
| Stored zip reader (`sources/archive/stored_zip_reader.h`): on-demand reads and folder listings of an uncompressed zip, holding only its directory in memory. Names match case-insensitively and reads are thread-safe. Compressed and zip64 archives are rejected | Archive path and entry names | Archive contents and when to open it |
| ZAPD extractor: pinned ZAPDTR, OTRExporter, and libgfxd projected for in-process O2R extraction | Pinned sources, resource-type and animation headers, asset XML from the asset folder or one stored zip (`Request.assetBundle`), and a loaded ROM | Asset folder or bundle, version and MQ selection, archive names, the calling thread, helper count, and progress display |
| Async tasks, thread pool, clock, and libultra timer | Pinned thread-pool input and Xbox synchronization APIs | Task admission, ownership, and shutdown order |
| Game/render overlap: frame channel, frame worker, deferred storage, writable spans, replay cadence, and light depth sampling | Packet type, game frame callback, producer hardware thread, and depth read callbacks | Game hooks, packet contents, GUI and audio synchronization, and shutdown order |
| JSON, libzip, CRT, value types, and compiler projections | Locked upstream sources and parser dependencies | Version adaptation, serializers, archives, and generated-file staging |
| Fast3D math, caches, and vertex helpers | Pinned interpreter types and source anchors | Layout, capacity, formulas, and observation points |

ZAPD extraction runs in process on the calling thread plus up to three helper
threads, each with a small-block arena (`sources/zapd/arena.cpp`). The helpers
run on hardware threads 4, 5 and 2 and stop taking files when free memory drops
below 96 MB. `Request.stages` receives one line of seconds per stage, summed
over threads.

Shared JSON vector conversion reserves destination storage before copying
elements. It keeps element order, type checks, and allocation-failure order.
Normal-batch helpers leave empty output untouched. A singleton uses its own
normal and does not populate the lookup table.

## Renderer capabilities

Adapters choose these definitions when compiling `sources/xenos/core.cpp`.
Every name below has the prefix `SHIPYARD_XENOS_`. The release column is
`xbox360_platform.xenos_capabilities.compiler_options`; setting
`SHIPYARD_XENOS_DIAGNOSTICS=1` selects its instrumented build.
Warwing and 2Ship3Shipsty call `compiler_options(3, shader_pack=True)`.
3Shipsty sets its options with its own copy of
`xenos_capabilities.compiler_options()`.

| Definition | Source default | Release (Warwing, 2Ship3Shipsty) | 3Shipsty |
| --- | --- | --- | --- |
| `SHADER_ABI` | 3 | 3 | 2 |
| `SHADER_PACK` | 0 | 1, with `shader_pack=True` | 1 |
| `DEFERRED_FRAMEBUFFERS` | 0 | 1 | 1 |
| `BINDING_REUSE` | 0 | 1 | 1 |
| `TILING_BUDGET` | 0 | 1 | 1 |
| `DIAGNOSTICS` | 1 | 0, or 1 instrumented | 0, or 1 for instrumented builds |
| `DETAILED_TIMING` | 1 | follows `DIAGNOSTICS` | 1 |
| `ALPHA_HISTOGRAM` | 0 | follows `DIAGNOSTICS` | 1, active in instrumented builds |
| `TILING_TRIALS` | 1 | 0 | 1 |

`SHIPYARD_XENOS_INDEXED_TRIANGLES` is defined by `sources/xenos/core.h` and
`sources/fast3d/indexed_submission.h`, not by adapters. It enables the indexed
draw interface (`DrawIndexedTriangles`) and the compact triangle row paths.

Shader ABI 3 supplies primitive depth and logical raster state. ABI 2 uses the
shader-pack format selected by 3Shipsty. Runtime shader lowering and pack
generation must pass the same ABI to `Shipyard_LowerShader`; its default is 3.
A pack consumer supplies `Shipyard_DecodeFast3DFeatures` and a program callback.
Fast3D consumers pass `Shipyard::Xenos::PackedFast3DProgram` and compile
`sources/xenos/shader_pack_program.cpp` with the same definitions as the core.

`xbox360_platform.shader_pack` writes packs. Version 3 keys programs by two
64-bit ids and stores each program's lowered-source SHA-256 and the build ABI.
On first use the runtime regenerates every v3 program and rejects any entry
whose source or float count differs. It drops the whole pack when the probe
program, ABI or SDK versions differ. Version 2 packs (32-bit second id, ABI 2)
remain readable and are checked by the probe alone. `shader_pack_harvest`
collects draw states, `shader_pack_keys` keys them from the consumer's own
`GenerateCC`, and `sources/graphics/shader_pack_sources.cpp` generates them.

Deferred framebuffers keep published depth, defer clears, and hold resolved
images until a draw. HiZ is enabled only with fresh depth. Binding reuse skips
repeated upload bindings, filtering changes, and noise-scale updates. Tiling
budget selects the reservation policy; tiling trials are a separate option.

Renderer counters live in `sources/xenos/metrics.h`. Warwing reads and resets
them with `ShipyardXenos::CaptureAndResetCounters`. 3Shipsty can compile them out.
Native-state checks are opt-in through `Shipyard_VerifyFast3DReuse`. Warwing uses
`SMASH360_VERIFY_NATIVE_REUSE=1`, and 3Shipsty defaults to disabled.

## Projector options

Opcode lowering accepts `composed_dispatch=True`. Resource lookup supports the
selected load signatures, optional owned-rvalue construction, and task draining.
Logging generation accepts `normalize_file_paths=False` when the adapter keeps
its own path handling. Version choices stay explicit in the adapter.
