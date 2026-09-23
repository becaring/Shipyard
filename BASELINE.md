# Shared Xbox 360 baseline

This base targets the real Ship of Harkinian 9.2.3 runtime. Smash360 consumes
shared facilities through its BattleShip-specific adapters. PaperBoat/Starfox64
is a future consumer, not an assumption that its game or Fast3D API matches SoH.

The first locked capabilities are the original Xenos implementation and
framebuffer extension, exact triangle-stream packing, and production C++
compiler settings. Both current ports must verify `baseline.json` and exercise
their own pinned upstream contracts. Version-specific shader/material logic,
resource formats, save semantics, and application UI stay with the port.

| Capability | Shared owner | SoH 9.x | Smash360 | State |
| --- | --- | --- | --- | --- |
| Xenos source and framebuffer extension | `sources/xenos` | current interface projection | BattleShip projection | both PPC objects compile |
| Exact vertex packing | `python/xbox360_platform/vertex_stream.py` | 21 writes, 12 colors | 25 writes, 17 colors | differential tests pass |
| Production C++ policy | `python/xbox360_platform/build_policy.py` | project generator | project generator | project tests pending |
| Audio thread affinity primitive | planned common async owner | 9.x mixer requests core 2 | existing mixer policy | 9.x PPC test passes; consolidation pending |
| Resource/archive concurrency | planned common contract | 9.x projection | donor tests | 9.x parity fixes pending |
| Shader-code persistence | planned common service | modern storage adapter required | existing path | 9.x pending |
| Hardware no-Watson boot | per-port runtime validation | first-chance fault unresolved | boots | unproven |

Do not promote a capability to shared simply because its names match. Each
migration requires source equivalence or a semantic differential, both
consumers building from the common file, and relevant runtime validation.
`baseline.json` is content-addressed; each consumer pins its hash and fails
closed on drift.
