# Interpolation recording

`sources/interpolation/recording_memory.h` was extracted from the private
3Shipsty commit `99a379bb1` (`platform/shipyard/interpolation/recording_memory.h`).
It retains the 32-byte bins, 4096-byte maximum cached block, 2 MiB per-thread
retention limit, and trim-before-retry allocation policy.

`python/xbox360_platform/interpolation_recording.py` supplies ownership,
allocation, operation-index and child-lookup projections. The index and child
projections derive from that commit's `tools/shipyard/prepare_interpolation.py`;
opcode conversion is an explicit consumer capability. Payload layouts, label
ordering, camera policy and matrix arithmetic remain consumer-owned.

The opt-in `retain_root_operations` projection preserves empty root operation
vectors across frame rotation, with an explicit byte budget per recording root.
Majora uses 2 MiB per root (two roots). This live-capacity budget is separate
from the freed-block pool. Child recordings and item order are still cleared;
oversized root vectors release their storage. This avoids repeated heap growth
for star recordings whose operation arrays exceed the pool's 4096-byte blocks.

Majora validates original-versus-projected replay, recursive index ownership,
pool bounds and allocation-failure recovery. Host contracts do not establish
console frame-rate improvements.
