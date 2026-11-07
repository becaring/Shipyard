# Shipyard

**The Xbox 360 platform under 3Shipsty and Warwing.**

Shipyard is the shared Xbox 360 code behind ports built on libultraship and Fast3D. It covers rendering on Xenos, XAudio2 audio, controller input, save storage, and building a game's archives from the player's ROM on the console. A game compiles Shipyard in from source; there is no standalone executable. Each game keeps its upstream adaptation and game behavior in its own repository.

## Games

| Game | Port of | Status |
|---|---|---|
| [3Shipsty](https://github.com/becaring/3Shipsty) | Ship of Harkinian 9.2.3 (Ocarina of Time) | Tested on hardware |
| [Warwing](https://github.com/becaring/Warwing) | BattleShip (Super Smash Bros. 64) | Tested on hardware |
| [2Ship3Shipsty](https://github.com/becaring/2Ship3Shipsty) | 2Ship2Harkinian 5.0.1 (Majora's Mask) | In testing |

## Reporting a problem

Shipyard has no players of its own. Report game problems in that game's repository, where the bug report form asks for the logs.

## Support Me
[<img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" height="40">](https://buymeacoffee.com/becaring) [<img src="https://storage.ko-fi.com/cdn/kofi3.png?v=3" height="36">](https://ko-fi.com/becaring)

If you had fun with 3Shipsty or Warwing, a donation is NEVER required but always appreciated <3

## Building and Using Shipyard

[Building and tests](docs/BUILDING.md) covers the host checks, [Consumers](docs/CONSUMERS.md) explains how a game pins and compiles Shipyard, and [Architecture](docs/ARCHITECTURE.md) describes what Shipyard owns and what stays with each game.

## Credits

Developed with AI coding assistants; design, integration and hardware validation by becaring.

## License

Original Shipyard code is [MIT](LICENSE); third-party source keeps its own license and is credited in [NOTICE](NOTICE.md), with its origins in the [provenance audit](provenance/SHIPYARD_PROVENANCE_AUDIT.md). The repository contains no ROMs, game assets or Xbox 360 SDK files.
