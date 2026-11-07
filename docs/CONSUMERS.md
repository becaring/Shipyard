# Consumer setup

Clone Shipyard as a sibling named `Shipyard` beside the game repository. The
game's `platform-base.lock.json` uses `"base": "../Shipyard"`. For another local
layout, set `XBOX360_PLATFORM_BASE` to the absolute Shipyard checkout path.
The variable only changes where Shipyard is found. The lock still controls the
required revision, API version, and manifest digest.

Use Python 3.9 or newer and Git. Add Shipyard's `python/` directory to the import
path and call `xbox360_platform.contract.verify(base, lock)` before using package
files. The base must be the Git checkout root. API 7 accepts one shared package
and rejects the `selection` field.

The lock records:

- `git_revision`: the full Shipyard commit ID.
- `api_version`: the package interface version.
- `baseline_sha256`: SHA-256 of the complete `baseline.json` file.

Verification checks the checkout root and HEAD, manifest digest, API version,
and every manifested file. Changes outside the manifest do not relax these
checks. Each game separately checks its upstream inputs and adapters.

To adopt a package change, review the source and generated differences, update
the baseline, run the relevant tests, and commit Shipyard first. Then update the
game's lock and verify its generated sources and build. Interface changes also
need coordinated API and adapter updates. Documentation edits do not change the
package API.

[Architecture](ARCHITECTURE.md) covers component ownership and capability
options. [Building](BUILDING.md) lists the test commands.
