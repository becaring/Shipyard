# Shipyard: main integration, 2026-09-29

Reviewed range: `103092d117c625053251bcb05ee5c3e54a4fa1df..0f446c26719bb47cd0c73fd2109f0272a99cd7bd`.

The recorded builds and tests belong to the range endpoint above. The
documentation-only follow-up does not replace the binary build identity.

Consolidate canonical Shipyard and the completed OoT/Majora selected histories
on main without flattening their different runtime contracts. Canonical sources
remain the Smash selection. Import the tracked OoT and Majora trees into
selections/3Shipsty and selections/2Ship, retaining their sources, projectors,
fixtures, tests, documentation and licenses. The merge records the prior
f70622bdac2eb2eabb6aa122c83132bb5782aad5 OoT and
426c3ee983b4bd6a2196e77762b471537ce785bd Majora histories as parents; their
original branches and worktrees remain available.

The integrated shared source changes include generation-aware texture cache
retirement, TMEM/palette-aware pipeline reload restoration and certification,
prepared CoordinateAxis arithmetic, and service-processor placement for async
and thread-pool workers. Presentation owns an asynchronous Xenos swap chain,
including startup presentation and release ordering. Selected variants preserve
their game-specific behavior; OoT includes published/deferred depth handling and
the resident program-keyed v2 shader pack, while Majora retains its own selected
resource and interpreter contracts. Existing shared controller, matrix,
interpolation, opcode and resource projection helpers are retained.

Update the selection contract to permit only the named 3Shipsty and 2Ship roots
under the owning Git checkout. Keep revision, API, baseline digest, production
hash and path-containment validation. Add tests for permitted named roots and
rejected arbitrary nested roots; normalize the controller fixture's final newline.
Recompute each baseline from its live selected files rather than copying review
workspace hashes. All three consumers pin this tested integration revision.

Validation: all three selection-contract suites pass five cases each and all
three baseline suites pass two cases each. Canonical texture-hash, thread-pool,
presentation-surface, coordinate and shader primitive-depth checks pass. Majora
selection presentation checks and OoT shader-cache-failure/variant checks pass.
Consumer builds succeed: OoT 1,846 PPC objects, Majora 1,830 PPC objects, and
Smash 486 game translation units plus 252 staged application/LUS units.
Signed-in isolated Xenia runs reach the main loop or scene for all three games.

Integration imports production C++ variant content without redesigning it.
Physical Xbox throughput remains unverified. This documentation commit does not
change production sources, baselines, consumer locks or the tested revision.

## Incorporated commits

```text
0f446c2 integration: consolidate tested game selections and histories on main
2d3a4da test: terminate controller route fixture with newline
f70622b Use resident program-keyed v2 shader pack for OoT
e5aa7ff Add OoT prebuilt Xenos shader pack lookup
426c3ee Integrate reviewed asynchronous presentation and selected depth publication
8603a7e Integrate reviewed asynchronous presentation and selected depth publication
adfc5c2 Integrate reviewed asynchronous presentation and selected depth publication
2e8a900 Integrate reviewed reload certification and shared coordinate parity
490ba16 Integrate reviewed reload certification and shared coordinate parity
ddeae4f Integrate reviewed reload certification and shared coordinate parity
a8b7b6d Integrate reviewed texture retirement, reload restoration and service placement
984f3c3 Integrate reviewed texture retirement, reload restoration and service placement
227e651 Integrate reviewed texture retirement, reload restoration and service placement
a2ce12e Prepare coordinate axes independently of triangle filtering
de8bb0c Integrate reviewed prepared coordinate arithmetic for Modern SoH
3401dc3 Preserve incorporated tiling, culling, and instruction storage changes
6f7d4e0 Expose a settled alternate cache predicate (B5)
85db595 Expose a settled alternate cache predicate (B5)
42f6688 Materialize derived matrices only at observations (B3)
b5620ee Materialize derived matrices only at observations (B3)
66f9c7e Materialize derived matrices only at observations (B3)
a3dd22d Prepare interpolation recording pairs once per update (A6)
ba90a81 Prepare interpolation recording pairs once per update (A6)
e38ff03 Prepare interpolation recording pairs once per update (A6)
745ea86 Adopt batched MSAA restore donor (A5)
005f346 Adopt batched MSAA restore donor (A5)
143c1cf Adopt logical sampler certificate donor (A4)
ff2477b Adopt logical sampler certificate donor (A4)
16e8280 Reuse controller port routes between routing changes (A3)
8cf893b Reuse controller port routes between routing changes (A3)
1b8e965 Reuse controller port routes between routing changes (A3)
c4a4f2d Reuse settled synchronous resource hits (A2)
57977d1 Adopt shared directory-storage elimination (A1)
e8d5722 Adopt shared directory-storage elimination (A1)
d13688c Remove unused Fast3D directory storage (A1)
b0f542f Avoid redundant framebuffer work and repeated configuration expansion
0162218 Avoid redundant Xenos state work and batch framebuffer restoration
6a31cf3 Avoid ready futures for cached tagged resources
cd1041c Separate detailed probes from native elapsed timing
d5e1740 Precompose Fast3D opcode dispatch tables
730b52f Add allocation-free timing accumulation
fe797cb Record native alpha histogram baseline
b521af3 Profile native alpha draw distribution
c6a2782 Retain bounded root recording capacity between frames
434ca87 Share dispatch and recording optimizations across Zelda consumers
6baaba8 Refresh Shipyard baseline for shard layout fix
ba9a686 Key XDK incremental shards by worker count
045682c Avoid ready futures and recording copies in synchronous paths
00b804d Allow native draws without experimental tiling paths
```
