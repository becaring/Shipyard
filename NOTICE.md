# Source provenance and release status

No file below has a verified redistribution grant for its local Xbox 360 adaptation. Do not publish this repository until ownership and the applicable upstream notices are resolved. The user's MIT authorization applies only to code they authored or have the right to license; it does not relicense upstream work.

| File | Recorded source | Rights status |
| --- | --- | --- |
| `sources/xenos/core.cpp` | Consolidated the modern 3Shipsty and Smash360 Xenos implementations; both derive from the pinned SoH360/Fast3D platform lineage | Donor authorship and applicable notices unverified |
| `sources/xenos/core.h` | Shared Xenos interface extracted from the active consumer contracts | Authorship and donor contribution rights unverified |
| `sources/xenos/metrics.h` | Extracted renderer metrics contract from active consumer code | Authorship and donor contribution rights unverified |
| `sources/xenos/pixels.h` | Extracted pixel conversion contract from active consumer code | Authorship and donor contribution rights unverified |
| `sources/xenos/shader_cache.h` | Adapted from Smash360 and SoH360 shader-cache interfaces; game path policy extracted | Donor authorship and permission unverified |
| `sources/xenos/shader_cache.cpp` | Adapted from Smash360 and SoH360 shader-cache implementation; game path policy extracted | Donor authorship and permission unverified |
| `sources/graphics/shader_lower.cpp` | Adapted from Smash360 shader-lowering work | Pre-move source and contributor permissions unverified |
| `sources/graphics/shader_lower.h` | Added with the package interface | Authorship unverified |
| `sources/imgui/imgui_xenos.cpp` | Adapted from SoH360 `platform/xbox360/imgui_xenos.cpp` | Donor authorship and permission unverified |
| `sources/imgui/shipyard_imgui_xenos.h` | Shipyard interface derived from the two active consumer renderer contracts | Authorship and donor contribution rights unverified |
| `sources/imgui/gamepad_bindings.inc` | Extracted from SoH360 `platform/xbox360/imgui_platform.cpp` | Donor authorship and permission unverified |
| `sources/imgui/platform_frame.h` | Shared platform-frame contract extracted from modern 3Shipsty and Smash360 | Authorship and donor contribution rights unverified |
| `sources/imgui/platform_frame.cpp` | Consolidated Xbox gamepad navigation and visible controller cursor from both active consumers | Donor authorship and permission unverified |
| `sources/input/physical.h` | Moved from modern 3Shipsty; normalized text matches Smash360's physical input header after identifier renaming | Donor authorship and permission unverified |
| `sources/input/physical.cpp` | Moved from modern 3Shipsty; normalized text matches Smash360's physical input implementation after identifier renaming | Donor authorship and permission unverified |
| `sources/input/xinput.cpp` | Moved from modern 3Shipsty; normalized text matches Smash360's XInput transport after identifier renaming | Donor authorship and permission unverified |
| `sources/async/native.h` | Moved from modern 3Shipsty; adapted from Smash360 native async declarations with optional affinity | Donor authorship and permission unverified |
| `sources/async/native.cpp` | Moved from modern 3Shipsty; adapted from Smash360 native async implementation with optional affinity | Donor authorship and permission unverified |
| `sources/async/future.h` | Moved from modern 3Shipsty; adapted from Smash360 future primitives with timed waits | Donor authorship and permission unverified |
| `sources/async/detached.h` | Moved from modern 3Shipsty detached-worker lifetime declaration | Authorship and permission unverified |
| `sources/async/detached.cpp` | Moved from modern 3Shipsty detached-worker lifetime implementation | Authorship and permission unverified |
| `sources/thread-pool/native_support.h` | Moved from modern 3Shipsty; adapted from Smash360 BS support | Donor authorship and permission unverified |
| `sources/audio/PcmBufferPool.h` | Moved from modern 3Shipsty; normalized implementation matched Smash360 after namespace and error-type adaptation | Donor authorship and permission unverified |
| `sources/audio/PcmBufferPool.cpp` | Moved from modern 3Shipsty; normalized implementation matched Smash360 after namespace and error-type adaptation | Donor authorship and permission unverified |
| `sources/audio/PcmLayout.h` | Consolidated modern 3Shipsty six-channel layout and Smash360 stereo policy capability | Donor authorship and permission unverified |
| `sources/audio/XAudio2AudioPlayer.h` | Adapted from modern 3Shipsty and Smash360 XAudio2 sink interfaces | Donor authorship and permission unverified |
| `sources/audio/XAudio2AudioPlayer.cpp` | Modern 3Shipsty raw 5.1-capable sink with Smash360 failure callback; hot-path diagnostics omitted | Donor authorship and permission unverified |
| `sources/audio/services.h` | Xbox audio service hooks extracted from active consumer contracts | Authorship and donor contribution rights unverified |
| `sources/clock/chrono.h` | Moved from modern 3Shipsty; namespace-neutralized form matches Smash360's chrono declarations | Donor authorship and permission unverified |
| `sources/clock/chrono.cpp` | Moved from modern 3Shipsty with Smash360's per-thread QPC frequency cache | Donor authorship and permission unverified |
| `sources/storage/device.h` | Extracted from modern 3Shipsty and Smash360 storage interfaces | Donor authorship and permission unverified |
| `sources/storage/device.cpp` | Consolidated modern 3Shipsty storage and Smash360 selector behavior | Donor authorship and permission unverified |
| `sources/storage/atomic_file.h` | Moved from modern 3Shipsty atomic save-publication contract | Donor authorship and permission unverified |
| `sources/storage/atomic_file.cpp` | Moved from modern 3Shipsty atomic save-publication implementation | Donor authorship and permission unverified |
| `sources/io/shader_worker.h` | Optional shader I/O worker interface extracted from Smash360 runtime | Donor authorship and permission unverified |
| `sources/io/shader_worker.cpp` | Adapted from Smash360/SoH360 save-worker lineage as a bounded shader-write queue | Donor authorship and permission unverified |
| `sources/filesystem/filesystem.h` | Moved from modern 3Shipsty; wider implementation supersedes Smash360's local filesystem interface | Donor authorship and permission unverified |
| `sources/filesystem/filesystem.cpp` | Moved from modern 3Shipsty; includes reparse-aware queries and race-safe file operations | Donor authorship and permission unverified |
| `sources/fast3d/unit_color.h` | Canonical color table and packing contract extracted from both active Fast3D projections | Donor authorship and permission unverified |
| `sources/json/json.cpp` | Modern 3Shipsty bounded nlohmann 3.12.0 backend, itself adapted from Smash360; modern parser/stream/ordered behavior retained | Adaptation authorship and contributor permission unverified |
| `sources/json/include/nlohmann/json.hpp` | Modern 3Shipsty bounded nlohmann 3.12.0 interface, adapted from Smash360 and upstream APIs | Preserve upstream nlohmann MIT notice; adaptation rights unverified |
| `sources/json/algorithm_support.h` | Adapted from Smash360 JSON algorithm support with modern stream behavior | Adaptation authorship and permission unverified |
| `sources/json/ordered_projection.hpp` | Modern 3Shipsty ordered-map projection for the bounded backend | Adaptation authorship and permission unverified |
| `sources/presentation/pacing.h` | Moved from modern 3Shipsty refresh-aware presentation accumulator | Donor authorship and permission unverified |
| `sources/presentation/xenos_pacing.h` | Shared Xenos presentation interval application extracted from both active windows | Donor authorship and permission unverified |
| `sources/crt/float.cpp` | Moved from modern 3Shipsty strict-float adaptation | Donor authorship and permission unverified |
| `sources/crt/format.cpp` | Consolidated modern 3Shipsty C99 formatting and Smash360 `%z`/variadic behavior | Donor authorship and permission unverified |
| `sources/crt/format.h` | Shared formatting interface derived from both active consumer contracts | Authorship and donor contribution rights unverified |
| `python/xbox360_platform/bs_thread_pool.py` | Consolidated source-locked projections from modern 3Shipsty and Smash360 | Local generator authorship and permission unverified; projected BS source is MIT-licensed |
| `python/xbox360_platform/json/*.py` | Consolidated modern 3Shipsty JSON projections and source-locked nlohmann algorithm extraction | Local generator authorship and permission unverified; upstream input retains nlohmann MIT notice |
| `third_party/nlohmann-3.12.0/*` | Selected original nlohmann 3.12.0 headers used as locked generator inputs | nlohmann MIT; original notice in `third_party/nlohmann-3.12.0/LICENSE.MIT` |
| `tests/bs_thread_pool_upstream.hpp` | BS thread-pool 4.1.0 by Barak Shoshany | MIT; original copyright notice retained and license in `tests/LICENSE-BS-thread-pool.txt` |
| `python/xbox360_platform/vertex_stream.py` | Extracted from 3Shipsty `platform/shipyard/fast3d/vertex_packing.py` | Authorship unverified |
| `python/xbox360_platform/__init__.py` | First recorded in Shipyard | Authorship unverified |
| `python/xbox360_platform/build_policy.py` | First recorded in Shipyard | Authorship unverified |
| `python/xbox360_platform/contract.py` | First recorded in Shipyard | Authorship unverified |

The SoH360 donor snapshot includes a Harbour Masters MIT notice for libultraship and a separate Emill/MaikelChan Fast3D notice restricting binary redistribution. Smash360's pinned BattleShip notice has different scope and does not license its local `platform/xbox360` files. Match notices to the exact incorporated source before redistribution. Preserve applicable copyright and license text; do not substitute a current upstream website's license for the pinned donor.

The initial local Git commit contains pre-cleanup renderer code. Before publishing, ensure the public history contains only cleared files and notices. This checkout does not include XDK SDK files, Nintendo assets, ROM-derived assets, generated binaries, or build output.
