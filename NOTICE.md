# Source provenance and release status

No file below has a verified redistribution grant for its local Xbox 360 adaptation. Do not publish this repository until ownership and the applicable upstream notices are resolved. The user's MIT authorization applies only to code they authored or have the right to license; it does not relicense upstream work.

| File | Recorded source | Rights status |
| --- | --- | --- |
| `sources/xenos/gfx_xenos.cpp` | Adapted from SoH360 `platform/xbox360/gfx_xenos.cpp` | Donor authorship and applicable notices unverified |
| `sources/xenos/native_extensions.inc` | Byte-identical to Smash360 `platform/xbox360/graphics/native_extensions.inc` | Donor authorship and permission unverified |
| `sources/graphics/shader_lower.cpp` | Adapted from Smash360 shader-lowering work | Pre-move source and contributor permissions unverified |
| `sources/graphics/shader_lower.h` | Added with the package interface | Authorship unverified |
| `sources/imgui/imgui_xenos.cpp` | Adapted from SoH360 `platform/xbox360/imgui_xenos.cpp` | Donor authorship and permission unverified |
| `sources/imgui/shipyard_imgui_xenos.h` | Shipyard interface derived from the two active consumer renderer contracts | Authorship and donor contribution rights unverified |
| `sources/imgui/gamepad_bindings.inc` | Extracted from SoH360 `platform/xbox360/imgui_platform.cpp` | Donor authorship and permission unverified |
| `sources/input/physical.h` | Moved from modern 3Shipsty; normalized text matches Smash360's physical input header after identifier renaming | Donor authorship and permission unverified |
| `sources/input/physical.cpp` | Moved from modern 3Shipsty; normalized text matches Smash360's physical input implementation after identifier renaming | Donor authorship and permission unverified |
| `sources/input/xinput.cpp` | Moved from modern 3Shipsty; normalized text matches Smash360's XInput transport after identifier renaming | Donor authorship and permission unverified |
| `python/xbox360_platform/vertex_stream.py` | Extracted from 3Shipsty `platform/shipyard/fast3d/vertex_packing.py` | Authorship unverified |
| `python/xbox360_platform/__init__.py` | First recorded in Shipyard | Authorship unverified |
| `python/xbox360_platform/build_policy.py` | First recorded in Shipyard | Authorship unverified |
| `python/xbox360_platform/contract.py` | First recorded in Shipyard | Authorship unverified |

The SoH360 donor snapshot includes a Harbour Masters MIT notice for libultraship and a separate Emill/MaikelChan Fast3D notice restricting binary redistribution. Smash360's pinned BattleShip notice has different scope and does not license its local `platform/xbox360` files. Match notices to the exact incorporated source before redistribution. Preserve applicable copyright and license text; do not substitute a current upstream website's license for the pinned donor.

The initial local Git commit contains pre-cleanup renderer code. Before publishing, ensure the public history contains only cleared files and notices. This checkout does not include XDK SDK files, Nintendo assets, ROM-derived assets, generated binaries, or build output.
