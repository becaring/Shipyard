# Shipyard source provenance and release conditions

The maintainer confirms that the original Xbox 360 implementation work in
their SoH360, 3Shipsty, Smash360, and Shipyard trees is their own. The
maintainer also confirms that `becaring` is their account. Moving that work
between local trees does not create another author or a permission dependency.
The MIT grant in `LICENSE` applies only to contributions for which the
maintainer holds the necessary rights. It does not relicense upstream code,
generated derivatives of upstream code, game assets, or SDK components.

## Current Shipyard source

At the frozen `e1e71554d9b64ff99165e68a1e2f25c66ca61915` baseline, all
174 tracked paths were classified in the provenance audit. The successor
hardening revision changes the diagnostic queue and optional shader cache,
adds tests, and corrects this notice. Its original local code remains within
the maintainer's stated MIT authorization, subject to any third-party
expression established by the lineage review below.
The path-by-path baseline inventory and preserved historical notices are in
`provenance/SHIPYARD_PROVENANCE_AUDIT.md` and its companion files.

| Scope | Origin and retained terms |
| --- | --- |
| 156 baseline paths in `sources/`, `python/`, tests, and repository documentation | Original local Xbox implementation or adaptation; maintainer MIT grant applies to rights they hold. The historical SoH360-to-Smash-to-Shipyard path is local development, not evidence of an outside donor. No evidence attributes the maintainer's Smash implementation to Bromurokkk's separate prototype. |
| Nine JSON implementation/projector paths | Local changes with selected nlohmann 3.12.0 inputs or derived algorithms. Preserve `third_party/nlohmann-3.12.0/LICENSE.MIT` and upstream copyright. |
| `sources/speech/data_boundary.c` and `.h` | Locally written eSpeak data/PCM boundary. eSpeak NG engine and voice data reside in 3Shipsty's separate pinned dependency; static linking has program-level obligations described below. |
| Four `third_party/nlohmann-3.12.0/include/` headers and its license | nlohmann 3.12.0 MIT source and notice. |
| `tests/bs_thread_pool_upstream.hpp` and `tests/LICENSE-BS-thread-pool.txt` | Barak Shoshany BS thread pool 4.1.0 MIT source and notice. |

The old SoH360 Fast3D snapshot, the later asset-exception notice, and the
3Shipsty and Warwing pinned Fast3D MIT notices are distinct. The old SoH360
`upstream/libultraship/libultraship/Lib/Fast3D/LICENSE.txt` says binary
redistribution is not allowed and has no asset exception. The full later
notice at `Emill/n64-fast3d-engine` commit
`881eb68bad1150f720433d75bccb4d26879c28ab` and at the maintainer's
`becaring/Re60-Legacy-Xenos-Renderer` commit
`23eb15e6bee171d6c1c2bd38350d222bedff0d4c`, `LICENSE.txt`, permits
binary redistribution when the binary contains no assets the distributor
lacks rights to distribute. The active consumers' pinned Fast3D trees carry
MIT notices naming Emill and MaikelChan. Keep each notice with the source
revision it governs; a matching project name alone does not choose a grant.

Exact-line comparisons of the historical and current renderer files against
the old upstream corpus found few matches. They support the source trail but
are not a complete derivation test: renaming, formatting, or restructuring
can erase exact matches. Creation history and comparison of the actual
implementations remain necessary.

The shared `sources/fast3d/triangle_pipeline.h` is integrated in both active
consumers. 3Shipsty uses its trait adapter and projection/composition tests;
Warwing uses its own trait adapter.

## Fast3D notice assignment for selected source (R1 closed)

The bounded source and build-input trace is
`provenance/FAST3D_R1_LINEAGE.md`. The 3Shipsty and Warwing interpreter,
combiner-decoding, and projected shader-formula inputs come from their
separately pinned Fast3D trees, each carrying the Emill/MaikelChan MIT notice.
The generated Prism shader builders retain that upstream origin. Shipyard's
shader provider delegates to those builders, while its Xenos backend and
numeric/cache/triangle mechanisms follow the maintainer's local SoH360 and
Smash360 implementation trail. The historical Re60 Fast3D combiner and SM3
shader-generator files are not selected into either current XEX build. The
older source-only and later asset-exception notices remain preserved for
their historical revisions. This bounded finding does not rely on a low
exact-line match count as proof of independent origin.

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
separately. Applicable upstream notices must remain attached in any released
source or binary package.
