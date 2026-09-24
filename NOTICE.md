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
| `sources/graphics/shader_provider.cpp` | Consolidated modern 3Shipsty and Smash360 Xbox shader-provider orchestration; upstream-version decode remains in consumer callbacks | Donor authorship and permission unverified |
| `sources/binary/compat.h` | Consolidated the active consumers' binarytools XDK support headers, which differed only in namespace spelling | Donor authorship and permission unverified |
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
| `sources/atomic/atomic.h` | Consolidated Smash360 port and Fast3D Xenon atomics with modern 3Shipsty and Smash360 logging counters; explicit conservative and diagnostic-read ordering policies | Donor authorship and permission unverified |
| `sources/thread-pool/native_support.h` | Moved from modern 3Shipsty; adapted from Smash360 BS support | Donor authorship and permission unverified |
| `sources/audio/PcmBufferPool.h` | Moved from modern 3Shipsty; normalized implementation matched Smash360 after namespace and error-type adaptation | Donor authorship and permission unverified |
| `sources/audio/PcmBufferPool.cpp` | Moved from modern 3Shipsty; normalized implementation matched Smash360 after namespace and error-type adaptation | Donor authorship and permission unverified |
| `sources/audio/PcmLayout.h` | Consolidated modern 3Shipsty six-channel layout and Smash360 stereo policy capability | Donor authorship and permission unverified |
| `sources/audio/XAudio2AudioPlayer.h` | Adapted from modern 3Shipsty and Smash360 XAudio2 sink interfaces | Donor authorship and permission unverified |
| `sources/audio/XAudio2AudioPlayer.cpp` | Modern 3Shipsty raw 5.1-capable sink with Smash360 failure callback; hot-path diagnostics omitted | Donor authorship and permission unverified |
| `sources/audio/services.h` | Xbox audio service hooks extracted from active consumer contracts | Authorship and donor contribution rights unverified |
| `sources/clock/chrono.h` | Moved from modern 3Shipsty; namespace-neutralized form matches Smash360's chrono declarations | Donor authorship and permission unverified |
| `sources/clock/chrono.cpp` | Moved from modern 3Shipsty with Smash360's per-thread QPC frequency cache | Donor authorship and permission unverified |
| `sources/clock/sleep.h` | Moved Smash360's finite nanosecond-to-SDK-wait helper into the shared clock boundary; caller scheduling remains consumer-owned | Donor authorship and permission unverified |
| `sources/storage/device.h` | Extracted from modern 3Shipsty and Smash360 storage interfaces | Donor authorship and permission unverified |
| `sources/storage/device.cpp` | Consolidated modern 3Shipsty storage and Smash360 selector behavior | Donor authorship and permission unverified |
| `sources/storage/atomic_file.h` | Moved from modern 3Shipsty atomic save-publication contract | Donor authorship and permission unverified |
| `sources/storage/atomic_file.cpp` | Moved from modern 3Shipsty atomic save-publication implementation | Donor authorship and permission unverified |
| `sources/io/shader_worker.h` | Optional shader I/O worker interface extracted from Smash360 runtime | Donor authorship and permission unverified |
| `sources/io/shader_worker.cpp` | Adapted from Smash360/SoH360 save-worker lineage as a bounded shader-write queue | Donor authorship and permission unverified |
| `sources/filesystem/filesystem.h` | Moved from modern 3Shipsty; wider implementation supersedes Smash360's local filesystem interface | Donor authorship and permission unverified |
| `sources/filesystem/filesystem.cpp` | Moved from modern 3Shipsty; includes reparse-aware queries and race-safe file operations | Donor authorship and permission unverified |
| `sources/fast3d/unit_color.h` | Canonical color table and packing contract extracted from both active Fast3D projections | Donor authorship and permission unverified |
| `sources/fast3d/value_init.h` | Consolidated modern 3Shipsty and Smash360 Fast3D value-initialization helper | Donor authorship and permission unverified |
| `sources/fast3d/command_stack.h` | Moved Smash360's cached command-stack container into the shared Fast3D boundary; modern 3Shipsty now consumes the same physical header | Donor authorship and permission unverified |
| `sources/fast3d/clip_quantize.h`, `sources/fast3d/clip_quantize.cpp` | Consolidated guarded VMX clip and matrix-quantization mechanics from the active Fast3D adaptations; near-plane and loaded-vertex layout remain explicit consumer policy | SoH-derived donor authorship and applicable Fast3D notices unverified |
| `sources/fast3d/vertex_transform.h`, `sources/fast3d/vertex_transform.cpp` | Consolidated bounded 4-coordinate VMX vertex transform and required guards from modern 3Shipsty and Smash360 | Donor authorship and permission unverified; game/version-specific aspect and vertex publication remain with consumers |
| `sources/fast3d/vmx_load.h` | Shared unaligned VMX load used by the selected transform and Smash clip/quantize adapter | Donor authorship and permission unverified |
| `sources/libultra/clock.h`, `sources/libultra/clock.cpp` | Extracted modern 3Shipsty N64-cycle conversion; now uses the shared Shipyard clock | Donor authorship and permission unverified |
| `sources/libultra/timer.h`, `sources/libultra/timer.cpp` | Moved modern 3Shipsty libultraship fallback timer mechanics; Smash's separate VI scheduler is not included | Donor authorship and permission unverified |
| `sources/network/transport.h`, `sources/network/transport.cpp` | Moved modern 3Shipsty Xbox socket transport; optional to consumers without a socket service | Donor authorship and permission unverified |
| `sources/network/flag.h` | Split from modern 3Shipsty Xbox network support | Donor authorship and permission unverified |
| `sources/speech/data_boundary.c`, `sources/speech/data_boundary.h` | Moved from modern 3Shipsty's Xbox speech data loader; encodes eSpeak 1.52 file layouts but does not include the eSpeak engine or voice assets | Derivation, donor authorship and applicable eSpeak notices unverified |
| `sources/speech/SpeechQueue.h` | Moved modern 3Shipsty's bounded Xbox speech queue, with the default language changed to an explicit caller argument | Donor authorship and permission unverified |
| `sources/libzip/native_boundary.h`, `sources/libzip/native_boundary.cpp` | Modern 3Shipsty and Smash360 Xbox libzip boundaries were identical after exported-symbol renaming | Donor authorship and permission unverified; libzip upstream code is not included in these files |
| `sources/value_types/xbox_compat/common.h`, `sources/value_types/xbox_compat/optional.h` | Byte-identical modern 3Shipsty and Smash360 XDK value-type compatibility headers, normalized to LF for the package | Donor authorship and permission unverified |
| `sources/value_types/xbox_compat/variant.h` | Canonical generated variant companion for the two consumers' pinned 19-arity XDK value-type contract | Generator and donor authorship/permission unverified |
| `sources/prism/compat.h` | Byte-identical modern 3Shipsty and Smash360 Prism/XDK compatibility header | Donor authorship and permission unverified |
| `sources/json/json.cpp` | Modern 3Shipsty bounded nlohmann 3.12.0 backend, itself adapted from Smash360; modern parser/stream/ordered behavior retained | Adaptation authorship and contributor permission unverified |
| `sources/json/include/nlohmann/json.hpp` | Modern 3Shipsty bounded nlohmann 3.12.0 interface, adapted from Smash360 and upstream APIs | Preserve upstream nlohmann MIT notice; adaptation rights unverified |
| `sources/json/algorithm_support.h` | Adapted from Smash360 JSON algorithm support with modern stream behavior | Adaptation authorship and permission unverified |
| `sources/json/ordered_projection.hpp` | Modern 3Shipsty ordered-map projection for the bounded backend | Adaptation authorship and permission unverified |
| `sources/presentation/pacing.h` | Moved from modern 3Shipsty refresh-aware presentation accumulator | Donor authorship and permission unverified |
| `sources/presentation/xenos_pacing.h` | Shared Xenos presentation interval application extracted from both active windows | Donor authorship and permission unverified |
| `sources/math/color.h` | Consolidated modern 3Shipsty NaN-aware scalar clamp and Smash360 color-byte conversion used by both current UI projections | Donor authorship and permission unverified |
| `sources/memory/address_gap.h` | Moved Smash360's saturating address-interval exclusion helper; display-list registry and cache policy remain consumer-owned | Donor authorship and permission unverified |
| `sources/presentation/xenos_surface.h` | Shared presentation-surface contract extracted from modern 3Shipsty and Smash360 window adapters | Authorship and donor contribution rights unverified |
| `sources/presentation/xenos_surface.cpp` | Consolidated display-mode, pacing, present, and frame-latency mechanics from both active windows | Donor authorship and permission unverified |
| `tests/presentation/xtl.h` | Local fake XDK display-device declarations for the presentation contract | Local test authorship and permission unverified |
| `tests/presentation/surface_cases.cpp` | Local fake-device presentation contract cases | Local test authorship and permission unverified |
| `sources/crt/float.cpp` | Moved from modern 3Shipsty strict-float adaptation | Donor authorship and permission unverified |
| `sources/crt/format.cpp` | Consolidated modern 3Shipsty C99 formatting and Smash360 `%z`/variadic behavior | Donor authorship and permission unverified |
| `sources/crt/format.h` | Shared formatting interface derived from both active consumer contracts | Authorship and donor contribution rights unverified |
| `python/xbox360_platform/bs_thread_pool.py` | Consolidated source-locked projections from modern 3Shipsty and Smash360 | Local generator authorship and permission unverified; projected BS source is MIT-licensed |
| `python/xbox360_platform/c89_lower.py` | Adapted from the byte-identical `SoH360/tools/c89_lower.py` copies previously active in modern 3Shipsty and Smash360; source selection and float-union policy are now caller parameters | Donor authorship and permission unverified |
| `python/xbox360_platform/json/__init__.py` | Shipyard JSON projection package marker | Authorship unverified |
| `python/xbox360_platform/json/backend.py` | Consolidated modern 3Shipsty bounded JSON backend projection | Local generator authorship and permission unverified; upstream input retains nlohmann MIT notice |
| `python/xbox360_platform/json/generate_algorithms.py` | Source-locked extraction of nlohmann lexical/numeric routines | Local generator authorship and permission unverified; upstream input retains nlohmann MIT notice |
| `python/xbox360_platform/json/generate_ordered.py` | Source-locked ordered-map projection for the bounded backend | Local generator authorship and permission unverified; upstream input retains nlohmann MIT notice |
| `python/xbox360_platform/json/generate_streams.py` | Source-locked serializer/UTF-8 projection for the bounded backend | Local generator authorship and permission unverified; upstream input retains nlohmann MIT notice |
| `python/xbox360_platform/libzip.py` | Consolidated modern 3Shipsty and Smash360 libzip 1.11.4 XDK projections, retaining pinned upstream C inputs in the consumers | Local generator authorship and permission unverified; libzip upstream code is not in this module |
| `python/xbox360_platform/value_types.py` | Consolidated the active consumers' XDK variant projection for their pinned upstream contracts | Local generator authorship and permission unverified |
| `third_party/nlohmann-3.12.0/include/nlohmann/detail/conversions/to_chars.hpp` | Original nlohmann 3.12.0 algorithm input, LF-normalized | nlohmann MIT; original notice in `third_party/nlohmann-3.12.0/LICENSE.MIT` |
| `third_party/nlohmann-3.12.0/include/nlohmann/detail/input/lexer.hpp` | Original nlohmann 3.12.0 algorithm input, LF-normalized | nlohmann MIT; original notice in `third_party/nlohmann-3.12.0/LICENSE.MIT` |
| `third_party/nlohmann-3.12.0/include/nlohmann/detail/output/serializer.hpp` | Original nlohmann 3.12.0 algorithm input, LF-normalized | nlohmann MIT; original notice in `third_party/nlohmann-3.12.0/LICENSE.MIT` |
| `third_party/nlohmann-3.12.0/include/nlohmann/ordered_map.hpp` | Original nlohmann 3.12.0 ordered-map input, LF-normalized | nlohmann MIT; original notice in `third_party/nlohmann-3.12.0/LICENSE.MIT` |
| `third_party/nlohmann-3.12.0/LICENSE.MIT` | Original nlohmann MIT license text | nlohmann MIT |
| `third_party/nlohmann-3.12.0/README.md` | Local source-lock and LF-normalization receipt for the selected nlohmann inputs | Local authorship and permission unverified |
| `tests/bs_thread_pool_upstream.hpp` | BS thread-pool 4.1.0 by Barak Shoshany | MIT; original copyright notice retained and license in `tests/LICENSE-BS-thread-pool.txt` |
| `python/xbox360_platform/vertex_stream.py` | Extracted from 3Shipsty `platform/shipyard/fast3d/vertex_packing.py` | Authorship unverified |
| `python/xbox360_platform/cpp_syntax.py` | Consolidated modern 3Shipsty and Smash360 Fast3D if-initializer and member-default AST lowering | Local generator authorship and permission unverified |
| `python/xbox360_platform/fast3d_opcode.py` | Consolidated the modern 3Shipsty AST opcode-table lowerer and Smash360's equivalent opcode-table projection; upstream handler rows remain in each consumer | Local generator authorship and permission unverified |
| `python/xbox360_platform/fast3d_triangle_split.py` | Consolidated Smash360's cull/emission split projection and applied it to modern 3Shipsty through version-specific boundaries | Donor and local generator authorship and permission unverified |
| `tests/test_cpp_syntax.py` | Shipyard regression fixture for shared C++ syntax lowering | Test authorship and permission unverified |
| `tests/command_stack_contract.cpp` | Adapted Smash360's original-vs-projected stack-operation differential fixture for both active consumers | Test authorship and permission unverified |
| `sources/fast3d/dispatch_owner.h` | Consolidated the active interpreters' per-command-walk weak-owner pin and nested invalidation mechanics | Donor authorship and permission unverified |
| `tests/dispatch_owner_contract.cpp` | Shared lifetime, nesting and setter-invalidation contract for the dispatch-owner helper | Test authorship and permission unverified |
| `sources/fast3d/shade_cache.h` | Adapted Smash360's lazy, per-interpreter directional-light cache, descended from the historical SoH360 cache; light color layout is a caller adapter | Historical donor and local adaptation rights unverified |
| `sources/fast3d/texture_bindings.h` | Shared six-slot binding and sampler certificate, derived from the active consumers' Fast3D binding contracts | Local implementation and donor contribution rights unverified |
| `sources/fast3d/triangle_pipeline.h` | Smash360-derived Fast3D reconciliation certificate with explicit version-trait calls; modern SoH integration remains unverified | Smash360 donor authorship and permission unverified |
| `tests/texture_bindings_contract.cpp` | Six-slot alias, texture-ID and invalidation contract for the shared tracker | Test authorship and permission unverified |
| `tests/test_texture_bindings.py` | Host compile and execution of the shared texture-binding contract | Test authorship and permission unverified |
| `python/xbox360_platform/shade_cache.py` | Moved the Smash360 checked shade-cache projection into Shipyard with an explicit light-color accessor | Local projection authorship and permission unverified |
| `python/xbox360_platform/__init__.py` | First recorded in Shipyard | Authorship unverified |
| `python/xbox360_platform/build_policy.py` | First recorded in Shipyard | Authorship unverified |
| `python/xbox360_platform/parallel_build.py` | Shared deterministic XDK project sharding and compile-receipt verifier for active consumers | Local implementation authorship and permission unverified |
| `tests/test_parallel_build.py` | Fake-project sharding, concurrent runner and receipt contract | Local test authorship and permission unverified |
| `python/xbox360_platform/contract.py` | First recorded in Shipyard | Authorship unverified |

The SoH360 donor snapshot includes a Harbour Masters MIT notice for libultraship and a separate Emill/MaikelChan Fast3D notice restricting binary redistribution. Smash360's pinned BattleShip notice has different scope and does not license its local `platform/xbox360` files. Match notices to the exact incorporated source before redistribution. Preserve applicable copyright and license text; do not substitute a current upstream website's license for the pinned donor.

The initial local Git commit contains pre-cleanup renderer code. Before publishing, ensure the public history contains only cleared files and notices. This checkout does not include XDK SDK files, Nintendo assets, ROM-derived assets, generated binaries, or build output.
