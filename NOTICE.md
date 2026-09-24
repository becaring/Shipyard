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
| `sources/speech/data_boundary.c` and `.h` | Locally written eSpeak data/PCM boundary. eSpeak NG engine and voice data reside in Modern's separate pinned dependency; static linking has program-level obligations described below. |
| Four `third_party/nlohmann-3.12.0/include/` headers and its license | nlohmann 3.12.0 MIT source and notice. |
| `tests/bs_thread_pool_upstream.hpp` and `tests/LICENSE-BS-thread-pool.txt` | Barak Shoshany BS thread pool 4.1.0 MIT source and notice. |

The old SoH360 Fast3D snapshot, the later asset-exception notice, and the
active Modern/Smash pinned Fast3D MIT notices are distinct. The old SoH360
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
consumers. Modern uses its trait adapter and projection/composition tests;
Smash uses its own trait adapter. This is source and build evidence, not
physical hardware validation.

## Exact unresolved release facts

| ID | Missing fact | Required resolution |
| --- | --- | --- |
| R1: Fast3D notice applicability | Which protectable expression, if any, from each historical Fast3D revision is in current Shipyard implementations or selected XEX link inputs. The `23eb15e6` repository and license are identified; applicability to later code is not. | Compare actual implementations and creation history across the old SoH360 snapshot, becaring renderer, official Fast3D revisions, and active MIT-pinned consumers. Retain the applicable notice for each component and inspect any release payload against the asset exception where it governs. |
| R2: Modern combined XEX | Complete GPL-3.0-or-later obligations from statically linking eSpeak NG 1.52.0, including the covered program's Corresponding Source, local changes, build/projection/install/run scripts, notices, and license compatibility of other components. This interacts with R3 and R5. | Review the complete selected link and source inputs under GPL `COPYING` sections 5 and 6. Analyze any Xbox SDK System Library exception unit by unit; it neither automatically requires publishing the SDK nor exempts all SDK code. Prepare a complete compliant source/package process for the exact XEX and obtain open-source licensing counsel review before distribution. An eSpeak-only source bundle is insufficient for a covered combined executable. Standalone Shipyard's original MIT grant need not change. |
| R3: XEX/package contents | Exact ROM-derived assets, archives, SDK redistributables, and distribution grants in the proposed Modern or Smash package. | Inventory the frozen or successor XEX and each support file, then establish rights and SDK channel/terms for the actual package. |
| R5: consumer source publication | Rights and terms for upstream game/decomp content outside the maintainer's Xbox contributions. Smash's BattleShip MIT notice excludes its decomp submodule and named derivative tables; Modern has no top-level license file in its pin. | Define the exact source-release scope and clear those components independently of Shipyard's local grant. |

The source-publication review and executable/package review are separate.
No public release or XEX distribution is implied by this notice. Historical
pre-cleanup Git content requires content and rights review, but its presence
alone is not a reason to rewrite history. Applicable upstream notices must
remain attached in any released source or binary package.
