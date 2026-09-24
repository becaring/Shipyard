# Shipyard provenance audit and frozen-candidate errata

This is the historical audit of the frozen baseline. The successor source
candidate carries a corrected root `NOTICE.md` and maintainer-scoped `LICENSE`.
The three source-review findings below describe the frozen baseline, not a
claim about successor verification. Read the successor build and test receipts
before treating any finding as closed.

2026-09-24. This is a read-only addendum to the frozen source state. It corrects
the rights and integration status in Shipyard `NOTICE.md` without changing that
tracked file, the three source revisions, or either XEX. It is not a release
license file. A publication revision must carry the corrected notices in its
actual distributed source tree.

**Successor disposition:** The historical R1 and R4 rows below describe the
frozen baseline before its documentation was corrected. For this local
publication-notice candidate, R1 is closed by the selected-input trace in
`FAST3D_R1_LINEAGE.md`, and `NOTICE.md` plus `LICENSE` correct R4. The
remaining release questions are R2 (the combined Modern GPL-linked program),
R3 (exact binary/package assets and SDK terms), and R5 (consumer game/decomp
source rights). No source or executable has been published.

## Frozen identity

| Checkout/artifact | Identity |
| --- | --- |
| Shipyard | `e1e71554d9b64ff99165e68a1e2f25c66ca61915` |
| Modern 3Shipsty | `297119dc619c2b1b74cc2a7750d4382906a78f72` |
| Smash360 | `a07e0db3adb9f3ed82561a98f8a4ea1cd7ee0c6e` |
| Modern frozen XEX | `b850844e4b4e730cf029e8a8219235fe29baae2fdef0977e9d7c435710306f09` |
| Smash frozen XEX | `399dfd51e16511c689a0cca17bcd0980799b079eecfed8ba3531f0acafee50a2` |

## Method and ownership finding

All 174 tracked paths, including the 131 physical paths represented by the
current `NOTICE.md` table, were compared with the Shipyard Git addition
history, recorded local donor paths, the SoH360 snapshot, both pinned consumer
source trees, and retained third-party notices. All 174 paths first entered
Shipyard in commits authored as Cris (166) or Codex (8). That fact alone proves
only the move into Shipyard. The
maintainer's direct account identifies the original SoH360, 3Shipsty,
Smash360, and Shipyard Xbox development as their own work. Where the content
is an original part of that development, their existing MIT authorization
applies. A move through one of those local trees does not create a separate
author or a new permission dependency. Neither the source table nor the
searches of the active Xbox adaptation trees supplied evidence that the
maintainer's Smash implementation came from Bromurokkk's separate prototype;
no attribution or permission dependency is assigned to that prototype.

The current table's blanket `donor authorship/permission unverified` status
is therefore superseded. The path-by-path disposition is in the appendix:

- `L` (156 paths): original local Xbox implementation, adapter, generator, test,
  repository metadata, or documentation, covered by the maintainer's stated
  MIT authorization for rights they hold. The fake XDK declarations in
  `tests/*-sdk/` are locally authored test stand-ins, not copied SDK files.
  Upstream interfaces used by the consumer do not change this source
  origin. Any copied third-party expression found later retains its own terms.
- `J` (9 paths): local nlohmann-compatible implementation or source-locked
  projector, with upstream nlohmann 3.12.0 inputs or generated algorithms.
  The local contribution has the same maintainer authorization; the upstream
  material remains under nlohmann's MIT notice. `json/__init__.py` itself is
  only a local package marker, but is kept in this family for inventory.
- `E` (2 paths): locally written eSpeak data/PCM endian boundary. The source
  uses documented eSpeak 1.52 data layouts and calls into the separately
  pinned engine; the eSpeak engine and voice assets are not in Shipyard. The
  Modern consumer statically compiles eSpeak NG 1.52.0 under GPL-3.0-or-later,
  a separate distribution condition.
- `T` (7 paths): actual third-party material. Four selected nlohmann 3.12.0
  headers and its MIT license are under `third_party/nlohmann-3.12.0/`;
  `tests/bs_thread_pool_upstream.hpp` and `tests/LICENSE-BS-thread-pool.txt`
  are Barak Shoshany's BS thread pool 4.1.0 source and notice. These are not offered as
  maintainer-authored code. The local nlohmann README is `L`.

The historical first Shipyard commit `78dde519ad708c39bba4e85fe087f35c82c68c5a`
contained `sources/xenos/gfx_xenos.cpp` and `native_extensions.inc`, both
removed from the current tree. The former has all 1,192 lines identical to
the local SoH360 `platform/xbox360/gfx_xenos.cpp` after line-ending
normalization. Thus its historical presence does not, by itself, establish
an outside author or call for rewriting Git history. An exact substantive-line
comparison of both historical blobs against the old pinned upstream Fast3D
corpus found only one shared generic `for` loop in `gfx_xenos.cpp` and no
shared long lines in `native_extensions.inc`. Exact-line matching is supporting
evidence, not a complete derivation test: renaming, reformatting, or restructuring
copied code can remove the matches. Creation history and comparison of the
actual implementations carry more weight. On that source trail and the
maintainer's authorship account, the historical blobs are classified as local
Xbox contributions; there is no separate history-permission blocker.

For the highest-risk Fast3D/Xenos lineage, an exact-line scan of Shipyard
`sources/fast3d/` and `sources/xenos/` against the pinned old SoH360 upstream
Fast3D C/C++ corpus found only three matching substantive-length lines, all
generic loops. This is evidence against a verbatim transfer of upstream
engine implementation into those Shipyard files, but is not a complete
derivation test. The positive creation history and comparison of the actual
implementations matter more than the low matching-line count. The source
trail for the shared cache and triangle helpers is the maintainer's SoH360
platform files and Smash/Modern adapters, not an unidentified outside donor.

## Notices and distribution are separate questions

| Component | Source publication | Binary/package condition |
| --- | --- | --- |
| Shipyard original Xbox contribution | Maintainer's existing MIT authorization applies to their original rights. Put a real MIT notice in a future publishable source revision. | No special restriction from the local contribution itself; other linked components still govern. |
| nlohmann 3.12.0 selected source and derived/generated routines | MIT; retain `third_party/nlohmann-3.12.0/LICENSE.MIT` and its authorship notices. | Retain applicable MIT notices with packages that contain it. |
| BS thread pool 4.1.0 test input | MIT; retain `tests/LICENSE-BS-thread-pool.txt` and header copyright. | The selected generated runtime copy inherits the BS MIT notice. |
| Current Modern and Smash pinned Fast3D trees | `3Shipsty9.x/libultraship/{src,include}/fast/LICENSE.txt` and `Smash360/upstream/BattleShip/libultraship/{src,include}/fast/LICENSE.txt` say MIT, copyright Emill/MaikelChan. Preserve those exact notices. | Their pinned MIT notices permit binary distribution subject to including the notice; the frozen package still needs all other conditions checked. |
| Older SoH360 snapshot Fast3D | `SoH360/upstream/libultraship/libultraship/Lib/Fast3D/LICENSE.txt` (SHA-256 `2664ed6c027782b6264c2db874a2e894417eaa0f2494212d77462dff8bce55ec`) permits source redistribution with its notice. Its actual 19-line text says binary redistribution is not allowed; it contains no asset exception. | Do not silently substitute this older text for Modern/Smash's pinned MIT text, or vice versa. Whether any expression from this particular old engine revision is present in a frozen XEX must be established by source-to-object provenance, not the name of a local donor folder. |
| Later asset-exception Fast3D notice | The full notice at official `n64-fast3d-engine` commit `881eb68bad1150f720433d75bccb4d26879c28ab` was read and preserved as `fast3d-official-881eb68-LICENSE.txt` beside this audit (SHA-256 `7cba45a713b4323620d6c97e44d7ca5f59a262abddfb9d0f930410b9acbf29d8`). The old SoH360 notice is byte-equivalent to the official parent notice after line-ending normalization; `881eb68` updates that condition. The same asset-exception text was independently verified at `becaring/Re60-Legacy-Xenos-Renderer` commit `23eb15e6bee171d6c1c2bd38350d222bedff0d4c`, `LICENSE.txt`, and preserved beside this audit as `becaring-Re60-Legacy-Xenos-Renderer-23eb15e6-LICENSE.txt` (the same SHA-256). The maintainer confirms becaring is their account. The source notice/disclaimer requirement remains. | Its complete condition allows binary redistribution where the binary contains no assets the distributor lacks rights to distribute. It is a conditional exception, not a blanket prohibition or blanket permission. See `FAST3D_R1_LINEAGE.md` for current selected inputs and R3 for release-package assets. |
| Modern eSpeak NG 1.52.0, commit `4870adfa25b1a32b4361592f1be8a40337c58d6c` | GPL-3.0-or-later, retained in `scratch/speech-xdk/espeak-ng/COPYING`; Modern's `platform/shipyard/speech/README.md` records the pin. This applies to the engine, not automatically to standalone Shipyard's separate local data-boundary source. | Modern statically links the GPL engine. Distribution of the combined executable must be reviewed under GPL sections 5 and 6, including Corresponding Source for the covered combination, rather than only the engine checkout. Sonic is fetched during the build but is not linked into the runtime. |
| Consumer upstream and build dependencies | Modern libultraship, Smash BattleShip, Prism, spdlog/fmt, libzip, ImGui, StormLib, and other pinned inputs retain their own notices. Shipyard's projectors do not relicense their generated output. | Evaluate the exact linked units and shipped resource package. Neither a Shipyard MIT grant nor the Fast3D notice grants rights in Nintendo assets or Xbox SDK distribution. |

The old SoH360 Fast3D notice, Emill's later official asset-exception notice,
and the two active MIT Fast3D notices are different texts from different
revisions. `NOTICE.md` line 130 currently presents the old restriction as if
it determined both active consumers' binaries; that statement is not
supported by their pinned source. The official asset exception must be
evaluated against the code it licenses and the actual shipped payload.
No frozen XEX is being published.

## Exact unresolved facts and resolution

| ID | Affected scope | Missing fact | Required resolution |
| --- | --- | --- | --- |
| R1 | Any claim that the old source-only notice, later asset-exception notice, or active MIT Fast3D notices govern particular Shipyard implementations or either frozen XEX | The mapping of protectable Emill/MaikelChan expression from each historical revision into selected compile/link inputs. The historical citation is identified: `becaring/Re60-Legacy-Xenos-Renderer` commit `23eb15e6bee171d6c1c2bd38350d222bedff0d4c`, `LICENSE.txt`, in the maintainer's own repository, contains the asset exception. That identification alone does not establish the grant for later implementations. | Compare the actual incorporated implementations and creation history across the old SoH360 source, becaring historical renderer, official Fast3D revisions, and active MIT-pinned consumer sources; retain the applicable text for each component. If the asset-exception grant governs linked code, inspect the exact XEX and release package for assets without distribution rights. Keep all three distinct notice states. |
| R2 | Distribution of the complete Modern XEX with statically linked eSpeak NG | The precise GPL-3.0-or-later obligations for the combined program: Corresponding Source for the covered executable, local eSpeak changes, build/projection/install/run scripts, notices, and compatibility of every incorporated component. GPL `COPYING` sections 5 and 6 govern covered whole-work and object-code conveyance. This interacts with game/decomp scope in `R5` and SDK/component rights in `R3`. Whether a specific Xbox SDK unit qualifies as a GPLv3 System Library requires a separate fact-based analysis; neither publishing the SDK nor automatically exempting every SDK unit follows. | Identify all selected linked units and exact package inputs; determine the complete Corresponding Source and applicable terms for the combined Modern program, then prepare and verify an actual compliance package against the exact distributed XEX. Obtain open-source licensing counsel review before distribution. An eSpeak-only source bundle is insufficient if the combined executable is covered. Standalone Shipyard original MIT licensing need not change merely because its modules are linked into that combination. |
| R3 | Both XEXs and runtime data packages | Whether the contemplated distribution includes ROM-derived Nintendo assets or XDK redistributables, and the applicable Xbox SDK distribution grant/channel. The Modern manifest references `oot.o2r`, `soh.o2r`, a Djipi mod archive, and two translation `.otr` files outside its XEX; the Smash manifest includes `BattleShip.o2r` and `f3d.o2r`. Their mere separation from the XEX is not a rights determination. | Inventory the exact frozen XEXs and each named archive/support file; document rights in each shipped component and the applicable SDK distribution terms before distribution. An asset-free release set must exclude assets for which distribution rights are absent. |
| R4 | Source publication of the frozen Shipyard checkout | The tracked `NOTICE.md` still has false blanket ownership language and stale triangle status; no corrected license/notice set is present in the frozen source revision. | For a later publication candidate, commit a corrected `NOTICE.md` and MIT grant scoped to maintainer-owned code, retain nlohmann and BS notices, and reverify consumer locks. This audit addendum is the correction for the frozen state only. |
| R5 | Publication of complete Modern or Smash consumer sources | The rights for the upstream game/decomp portions have not been established by the maintainer's Xbox-platform authorization. Modern has no top-level license file in this pin. Smash's pinned `upstream/BattleShip/LICENSE` expressly excludes its `decomp/` submodule and named decomp-derived tables from BattleShip's MIT grant and says the upstream decomp has no explicit license. | Define the exact source-release scope. Obtain applicable rights/terms for the game/decomp source and listed derivative tables, or distribute only a source package whose contents are independently cleared; retain all upstream notices. This is separate from Shipyard's own local Xbox source grant and from XEX/asset distribution. |

There is no remaining unresolved item of the form “unknown author because the
file came from my other local tree.” The frozen table's R1 source match and
notice-version question has since been closed for selected successor inputs
in `FAST3D_R1_LINEAGE.md`. R2, R3, and R5 remain distribution/source-release
questions with the exact missing facts stated above.

## `NOTICE.md` integration errata

The `sources/fast3d/triangle_pipeline.h` row's “modern SoH integration remains
unverified” clause is false at the frozen revisions. Modern's
`platform/shipyard/fast3d/triangle_pipeline_projection.py` selects the shared
triangle certificate with Modern traits; its Modern-specific projection and
composition checks ran as part of the final 96 unit tests, followed by the
complete fresh PPC build and link. Smash uses its own trait adapter against
the same shared source. This is technical integration evidence, not proof of
hardware behavior. The row's “Smash360 donor permission unverified” language
is also superseded by the maintainer's ownership account and the source trail.

`NOTICE.md` line 3 (“No file below has a verified redistribution grant”) and
line 130 (old Fast3D binary restriction as the current release description)
should be replaced in a future publishable revision by the component-specific
table above. Package markers, tests, and newly written projectors are local
work under the maintainer authorization; their creation in Shipyard is not a
reason to infer a missing outside author. Third-party notices remain attached
to actual upstream code and generated derivatives. The blanket provenance
sentence in Modern's `porting/SHIPYARD_FINAL_STATUS.md` release-gate section is
likewise superseded by this audit; its technical build and hardware status
are unchanged.

## Three latest source-review findings: frozen disposition

| Finding | Frozen-code evidence and impact | Disposition |
| --- | --- | --- |
| Diagnostic callback exceptions | `sources/diagnostics/queue.cpp` calls `begin_record` and `accepted` while `gate_` is held and calls `worker_started`/`worker_stopped` without a catch boundary. Its `consume_record`/`flush_sink` catch invokes `sink_error` inside `sink_gate_` without guarding a second throw. An exception can strand a critical section or terminate the worker. Smash's current `BeginRecord` and `Accepted` only modify numeric metadata, but `WorkerStarted` logs and `Consume` builds a console line. | **Open in the frozen candidate.** Existing saturation/sink-failure tests do not inject exceptions from every callback. Do not claim this review finding was fixed. A change requires a new source revision, callback-failure contract tests, both fresh consumer builds/links, and new XEX identities. |
| Mutable record byte accounting | `Queue::submit` computes `bytes = source.bytes.size()+1` before `begin_record`, then charges `queued_bytes_` using that old size after copying the record; `accepted` receives a mutable queued record. `run_worker` subtracts its current `bytes.size()+1`. A callback changing `bytes` can corrupt the budget or underflow the counter. The current Smash callbacks only change timestamp/tick/thread metadata, so this is not evidence of an observed runtime regression. | **Open API-contract defect, currently unexercised by the selected adapter.** Enforce immutable payload or account the final owned size in a new candidate; add mutation/backpressure tests and reverify both consumers before claiming closure. |
| Optional shader-cache allocation failure | `sources/xenos/shader_cache.cpp::CodeCache::Store` catches allocation failure around the vector/map insertion, but later `payload.append` calls and key construction are outside that boundary. The optional persistence path can propagate `bad_alloc` rather than simply fall back to runtime shader compilation. | **Open in the frozen candidate.** Disablement/fallback under injected allocation failure needs a new candidate and targeted test, then fresh PPC build/link and runtime smoke evidence. The frozen XEX hashes remain unchanged. |

These findings are not silently treated as passed by the previous Shipyard
41-test total. Functional and physical acceptance can examine the frozen
candidates, but any fix that changes source must be identified and verified
as a different candidate.

## Inventory appendix

`L` = maintainer-authorized original local contribution; `J` = local JSON
implementation/projector plus retained nlohmann MIT scope; `E` = local eSpeak
format boundary with separate GPL engine dependency; `T` = incorporated
third-party MIT source/notice. A table generated from all 174 tracked files in
the frozen Shipyard revision follows.

| Path | Class |
| --- | --- |
| `.gitattributes` | L |
| `.gitignore` | L |
| `BASELINE.md` | L |
| `NOTICE.md` | L |
| `README.md` | L |
| `baseline.json` | L |
| `python/xbox360_platform/__init__.py` | L |
| `python/xbox360_platform/bs_thread_pool.py` | L |
| `python/xbox360_platform/build_policy.py` | L |
| `python/xbox360_platform/c89_lower.py` | L |
| `python/xbox360_platform/combiner_lookup.py` | L |
| `python/xbox360_platform/contract.py` | L |
| `python/xbox360_platform/cpp_syntax.py` | L |
| `python/xbox360_platform/fast3d_opcode.py` | L |
| `python/xbox360_platform/fast3d_triangle_certificate.py` | L |
| `python/xbox360_platform/fast3d_triangle_split.py` | L |
| `python/xbox360_platform/json/__init__.py` | J |
| `python/xbox360_platform/json/backend.py` | J |
| `python/xbox360_platform/json/generate_algorithms.py` | J |
| `python/xbox360_platform/json/generate_ordered.py` | J |
| `python/xbox360_platform/json/generate_streams.py` | J |
| `python/xbox360_platform/libzip.py` | L |
| `python/xbox360_platform/logging_core.py` | L |
| `python/xbox360_platform/parallel_build.py` | L |
| `python/xbox360_platform/prism.py` | L |
| `python/xbox360_platform/shade_cache.py` | L |
| `python/xbox360_platform/value_types.py` | L |
| `python/xbox360_platform/vertex_stream.py` | L |
| `sources/async/detached.cpp` | L |
| `sources/async/detached.h` | L |
| `sources/async/future.h` | L |
| `sources/async/native.cpp` | L |
| `sources/async/native.h` | L |
| `sources/atomic/atomic.h` | L |
| `sources/audio/PcmBufferPool.cpp` | L |
| `sources/audio/PcmBufferPool.h` | L |
| `sources/audio/PcmLayout.h` | L |
| `sources/audio/XAudio2AudioPlayer.cpp` | L |
| `sources/audio/XAudio2AudioPlayer.h` | L |
| `sources/audio/services.h` | L |
| `sources/binary/compat.h` | L |
| `sources/clock/chrono.cpp` | L |
| `sources/clock/chrono.h` | L |
| `sources/clock/sleep.h` | L |
| `sources/crt/float.cpp` | L |
| `sources/crt/format.cpp` | L |
| `sources/crt/format.h` | L |
| `sources/diagnostics/queue.cpp` | L |
| `sources/diagnostics/queue.h` | L |
| `sources/entropy/native.h` | L |
| `sources/environment/launch_environment.h` | L |
| `sources/fast3d/clip_quantize.cpp` | L |
| `sources/fast3d/clip_quantize.h` | L |
| `sources/fast3d/command_stack.h` | L |
| `sources/fast3d/coordinate.h` | L |
| `sources/fast3d/dispatch_owner.h` | L |
| `sources/fast3d/light_color_cache.h` | L |
| `sources/fast3d/material_value_cache.h` | L |
| `sources/fast3d/normal_sources.h` | L |
| `sources/fast3d/numeric.h` | L |
| `sources/fast3d/projection_cache.h` | L |
| `sources/fast3d/shade_cache.h` | L |
| `sources/fast3d/shader_metadata_cache.h` | L |
| `sources/fast3d/texture_bindings.h` | L |
| `sources/fast3d/triangle_pipeline.h` | L |
| `sources/fast3d/unit_color.h` | L |
| `sources/fast3d/value_init.h` | L |
| `sources/fast3d/vertex_transform.cpp` | L |
| `sources/fast3d/vertex_transform.h` | L |
| `sources/fast3d/vmx_load.h` | L |
| `sources/filesystem/filesystem.cpp` | L |
| `sources/filesystem/filesystem.h` | L |
| `sources/graphics/shader_lower.cpp` | L |
| `sources/graphics/shader_lower.h` | L |
| `sources/graphics/shader_provider.cpp` | L |
| `sources/imgui/gamepad_bindings.inc` | L |
| `sources/imgui/imgui_xenos.cpp` | L |
| `sources/imgui/platform_frame.cpp` | L |
| `sources/imgui/platform_frame.h` | L |
| `sources/imgui/shipyard_imgui_xenos.h` | L |
| `sources/input/physical.cpp` | L |
| `sources/input/physical.h` | L |
| `sources/input/xinput.cpp` | L |
| `sources/io/shader_worker.cpp` | L |
| `sources/io/shader_worker.h` | L |
| `sources/json/algorithm_support.h` | J |
| `sources/json/include/nlohmann/json.hpp` | J |
| `sources/json/json.cpp` | J |
| `sources/json/ordered_projection.hpp` | J |
| `sources/libultra/clock.cpp` | L |
| `sources/libultra/clock.h` | L |
| `sources/libultra/timer.cpp` | L |
| `sources/libultra/timer.h` | L |
| `sources/libzip/native_boundary.cpp` | L |
| `sources/libzip/native_boundary.h` | L |
| `sources/math/color.h` | L |
| `sources/memory/address_gap.h` | L |
| `sources/network/flag.h` | L |
| `sources/network/transport.cpp` | L |
| `sources/network/transport.h` | L |
| `sources/presentation/pacing.h` | L |
| `sources/presentation/xenos_pacing.h` | L |
| `sources/presentation/xenos_surface.cpp` | L |
| `sources/presentation/xenos_surface.h` | L |
| `sources/prism/compat.h` | L |
| `sources/speech/SpeechQueue.h` | L |
| `sources/speech/data_boundary.c` | E |
| `sources/speech/data_boundary.h` | E |
| `sources/storage/atomic_file.cpp` | L |
| `sources/storage/atomic_file.h` | L |
| `sources/storage/device.cpp` | L |
| `sources/storage/device.h` | L |
| `sources/thread-pool/native_support.h` | L |
| `sources/value_types/xbox_compat/common.h` | L |
| `sources/value_types/xbox_compat/optional.h` | L |
| `sources/value_types/xbox_compat/variant.h` | L |
| `sources/xenos/core.cpp` | L |
| `sources/xenos/core.h` | L |
| `sources/xenos/metrics.h` | L |
| `sources/xenos/pixels.h` | L |
| `sources/xenos/shader_cache.cpp` | L |
| `sources/xenos/shader_cache.h` | L |
| `tests/LICENSE-BS-thread-pool.txt` | T |
| `tests/atomic_file_contract.cpp` | L |
| `tests/bs_thread_pool_upstream.hpp` | T |
| `tests/combiner_lookup_contract.py` | L |
| `tests/command_stack_contract.cpp` | L |
| `tests/coordinate_contract.cpp` | L |
| `tests/diagnostic_queue_contract.cpp` | L |
| `tests/dispatch_owner_contract.cpp` | L |
| `tests/entropy_contract.cpp` | L |
| `tests/environment_contract.cpp` | L |
| `tests/io-sdk/xtl.h` | L |
| `tests/libultra_timer_contract.cpp` | L |
| `tests/light_color_cache_contract.cpp` | L |
| `tests/material_value_cache_contract.cpp` | L |
| `tests/network_flag_contract.cpp` | L |
| `tests/normal_sources_contract.cpp` | L |
| `tests/presentation/surface_cases.cpp` | L |
| `tests/presentation/xtl.h` | L |
| `tests/shader_metadata_cache_cases.cpp` | L |
| `tests/shader_worker_contract.cpp` | L |
| `tests/storage-sdk/xam.h` | L |
| `tests/storage-sdk/xtl.h` | L |
| `tests/storage_device_contract.cpp` | L |
| `tests/test_atomic_file.py` | L |
| `tests/test_baseline.py` | L |
| `tests/test_bs_thread_pool.py` | L |
| `tests/test_combiner_lookup_projection.py` | L |
| `tests/test_contract.py` | L |
| `tests/test_coordinate.py` | L |
| `tests/test_cpp_syntax.py` | L |
| `tests/test_diagnostic_queue.py` | L |
| `tests/test_entropy.py` | L |
| `tests/test_environment.py` | L |
| `tests/test_libultra_timer.py` | L |
| `tests/test_light_color_cache.py` | L |
| `tests/test_logging_provider.py` | L |
| `tests/test_material_value_cache.py` | L |
| `tests/test_network_flag.py` | L |
| `tests/test_parallel_build.py` | L |
| `tests/test_prism_projector.py` | L |
| `tests/test_shader_metadata_cache.py` | L |
| `tests/test_shader_worker.py` | L |
| `tests/test_storage_device.py` | L |
| `tests/test_texture_bindings.py` | L |
| `tests/test_triangle_certificate_projection.py` | L |
| `tests/texture_bindings_contract.cpp` | L |
| `third_party/nlohmann-3.12.0/LICENSE.MIT` | T |
| `third_party/nlohmann-3.12.0/README.md` | L |
| `third_party/nlohmann-3.12.0/include/nlohmann/detail/conversions/to_chars.hpp` | T |
| `third_party/nlohmann-3.12.0/include/nlohmann/detail/input/lexer.hpp` | T |
| `third_party/nlohmann-3.12.0/include/nlohmann/detail/output/serializer.hpp` | T |
| `third_party/nlohmann-3.12.0/include/nlohmann/ordered_map.hpp` | T |
