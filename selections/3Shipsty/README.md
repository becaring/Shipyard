# Shipyard

Shipyard provides shared Xbox 360 platform code and source projections for
LibUltraShip and Fast3D applications. It supplies the native renderer, audio,
input, storage and compiler compatibility mechanisms that ports can share while
keeping game behavior and upstream version differences in their own adapters.

Current consumers are [3Shipsty](https://github.com/becaring/3Shipsty), the modern
Ship of Harkinian port, and [Warwing](https://github.com/becaring/Warwing), the
BattleShip / Super Smash Bros. 64 port.

## Layout

- `sources/` contains native Xbox implementations and compatibility headers.
- `python/xbox360_platform/` contains pinned-source projectors and package verification.
- `tests/` contains projection, host and PowerPC contracts.
- `provenance/` and `third_party/` preserve source lineage and third-party notices.

Shipyard is compiled into a consumer; it does not build a standalone executable.
Consumers supply their pinned upstream headers, platform adapters, build project
and Xbox 360 SDK installation. See [architecture](docs/ARCHITECTURE.md) for the
interface boundaries.

## Use and development

Pin a Git revision, API version and manifest digest in the consumer's lock file,
then verify the selected checkout before generating or compiling sources.
[Consumer integration](docs/CONSUMERS.md) describes the lock contract and
[development](docs/DEVELOPMENT.md) covers tests and toolchain requirements.

Original contributions are covered by [LICENSE](LICENSE) to the extent of the
maintainer's rights. Upstream material retains its own terms; see
[NOTICE.md](NOTICE.md) and [provenance](provenance/SHIPYARD_PROVENANCE_AUDIT.md).
Shipyard does not supply game assets, ROMs or the Xbox SDK.
