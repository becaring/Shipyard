# Architecture

Consumers compile only the Shipyard components they select. Native services and
generic lowering belong here; application lifecycle, game data, pinned upstream
layout and version-specific formulas belong in consumer adapters. Generated
sources are outputs, not the maintained owners of a correction.

The [package boundary](../BASELINE.md) defines what the manifest covers.


Only select files required by the consumer's project. Include paths such as
`services.h`, `shader_provider.h`, and `xenos_backend.h` in the renderer are
consumer contracts, not missing Shipyard files.

| Shipyard owns | Required input | Consumer owns |
| --- | --- | --- |
| `sources/xenos/` renderer, resource lifetimes and native counters; `sources/graphics/` shader lowering | Xbox SDK and pinned Fast3D shader types through `shader_provider.h` | `services.h` logging, fatal handling, reuse verification and file writes; `xenos_backend.h` device boundary; shader feature decoding, display lifecycle and optional cache location |
| `sources/imgui/` native renderer and gamepad bindings | Xbox SDK and pinned `imgui.h` | Functions declared in `shipyard_imgui_xenos.h`, GUI lifecycle and input policy |
| `sources/audio/` XAudio2 sink and PCM pool | Xbox SDK and pinned `ship/audio/AudioChannelsSetting.h` | `ShipyardAudioLog`/`ShipyardAudioFatal`, audio producer and allowed channel layouts |
| `sources/diagnostics/` bounded record queue and worker | Xbox SDK synchronization primitives | Queue `Config` callbacks and context, sink, formatting, worker placement and shutdown timing |
| `sources/environment/`, `sources/storage/`, `sources/network/` | Xbox SDK filesystem, storage and socket APIs | Launch-file path and absence policy; title identity, mount/socket lifetime and failure policy |
| `sources/async/`, `sources/thread-pool/`, `sources/clock/`, `sources/libultra/` | Xbox SDK synchronization and timing APIs; pinned BS thread-pool projection where selected | Worker admission, task ownership, callback context and application shutdown order |
| `sources/json/`, `sources/libzip/`, `sources/crt/` | Pinned nlohmann/libzip inputs and XDK CRT behavior | Selected archives, serializers, generated output staging and game data policy |
| `sources/fast3d/` numeric, cache and lowering mechanisms | Pinned interpreter types and selected source anchors | Version-specific layout, formulas, capacity, observation and generated staging |
| `python/xbox360_platform/` source-locked projectors | Pinned BS, JSON, libzip, spdlog and Prism inputs as required by the selected generator | Dependency acquisition, parser where required, project generation, application policy and source-lock integration |

`sources/xenos/metrics.h` holds renderer counters accumulated on the graphics
thread. Smash reads and resets their public snapshot through
`ShipyardXenos::CaptureAndResetCounters`; Modern currently does not read it.
The counters are maintained operational diagnostics, not a separate profiler;
their integer increments occur in normal renderer builds even when a consumer
does not request a snapshot.
Native state and texture readback verification are opt-in through the
consumer's `Shipyard_VerifyFast3DReuse` callback: Smash binds it to
`SMASH360_VERIFY_NATIVE_REUSE=1`; Modern's setter is present but has no active
caller, so its default is disabled. `Shipyard_Log` and `Shipyard_Fatal` are
required operational hooks. Smash's full frame profiler is consumer-owned and
has its own build and runtime switches.

[NOTICE.md](../NOTICE.md) records the known source lineage and licensing status.

