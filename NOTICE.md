# Shipyard notices

The original Xbox 360 code in Shipyard is becaring's work, carried over from
their SoH360, Smash360 and 3Shipsty trees. It is released under the MIT grant in
`LICENSE`, which covers only contributions becaring holds the rights to. It does
not relicense upstream code, code generated from upstream code, game assets or
SDK components.

## Third-party code in this repository

| Files | Origin and terms |
| --- | --- |
| `third_party/nlohmann-3.12.0/` | nlohmann JSON 3.12.0, MIT. Keep `LICENSE.MIT` and the upstream copyright. |
| JSON implementation and projector files | Local changes built on nlohmann 3.12.0 inputs or algorithms, under the same notice. |
| `tests/bs_thread_pool_upstream.hpp` | BS thread pool 4.1.0 by Barak Shoshany, MIT, with `tests/LICENSE-BS-thread-pool.txt`. |
| `sources/speech/data_boundary.c` and `.h` | Local eSpeak data and PCM boundary. eSpeak NG is a 3Shipsty dependency under GPL-3.0-or-later. |

## Fast3D

Each game's Fast3D interpreter, combiner decoding and shader formulas come from
its own pinned libultraship tree, under that tree's MIT notice naming Emill and
MaikelChan. The generated Prism shader builders keep that origin, and Shipyard's
shader provider calls them. The Xenos backend in `sources/xenos/` and the
helpers in `sources/fast3d/` are becaring's code from SoH360 and Smash360.

`provenance/` keeps two older Fast3D notices for the revisions they govern:
Emill's n64-fast3d-engine at `881eb68bad1150f720433d75bccb4d26879c28ab` and the
same text at becaring's Re60 Legacy Xenos Renderer commit
`23eb15e6bee171d6c1c2bd38350d222bedff0d4c`. That notice permits binary
redistribution when the binary contains no assets the distributor lacks rights
to. The Re60 combiner and shader generator are not built into either game.

## Release distribution

Each game's release package carries a `licenses` folder with the license text
of the game, Shipyard and every component compiled into its XEX. Packages
contain no ROM data; the console builds the game archives from the player's
ROM. 3Shipsty statically links eSpeak NG 1.52.0, so a 3Shipsty release is
distributed under GPL-3.0-or-later, and its `SOURCE.txt` names the public
3Shipsty, Shipyard and eSpeak NG commits that are its corresponding source.

Upstream game and decomp source keeps its own terms in each game's repository.
Warwing's BattleShip MIT notice excludes its decomp submodule and named
derivative tables, and 3Shipsty's MIT `LICENSE` covers only 3Shipsty
contributions. Each XEX links Xbox 360 SDK libraries; no SDK files ship
separately. Keep the upstream notices with any source or binary release.
