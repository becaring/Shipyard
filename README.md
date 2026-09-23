# Shipyard

Shipyard is the shared Xbox 360 platform checkout for LibUltraShip/Fast3D
consumers. Consumers compile its reusable C++ implementations directly and
provide narrow interfaces for game/version-specific behavior. Some Fast3D,
GUI and runtime paths are still being migrated; see the active consumer
ownership reports before treating a subsystem as canonical. Shipyard requires
the consuming port's upstream headers and Xbox 360 SDK and does not produce
an XEX on its own.

## Contents

- `sources/xenos/`: directly compiled renderer, framebuffer and optional shader cache.
- `sources/graphics/`: directly compiled shader lowerer and provider orchestration.
- `sources/imgui/`: directly compiled ImGui renderer and shared gamepad bindings.
- `sources/input/`: directly compiled physical XInput backend.
- `sources/async/`: native synchronization, futures, and detached-worker lifetime.
- `sources/audio/`: XAudio2 sink, layout policy boundary and PCM pool.
- `sources/clock/`, `sources/libultra/`, `sources/crt/`, `sources/filesystem/`, `sources/storage/`: native platform services.
- `sources/network/`, `sources/libzip/`, `sources/io/`, `sources/speech/`: optional socket, archive, I/O and speech data/queue services.
- `sources/binary/`: XDK binarytools compatibility contract.
- `sources/value_types/`, `sources/prism/`: XDK value-type and Prism compatibility contracts.
- `sources/presentation/`, `sources/fast3d/`: shared pacing and bounded interpreter/geometry helpers.
- `sources/thread-pool/`: support for the bounded BS thread-pool projection.
- `python/xbox360_platform/bs_thread_pool.py`: source-locked BS 4.1.0 lowering.
- `python/xbox360_platform/json/`: source-locked nlohmann 3.12.0 projection.
- `python/xbox360_platform/libzip.py`: source-locked libzip 1.11.4 XDK projection.
- `python/xbox360_platform/value_types.py`: XDK variant projection.
- `python/xbox360_platform/c89_lower.py`: C89 lowering with consumer-supplied source policy.
- `python/xbox360_platform/vertex_stream.py`: vertex-stream source transform.
- `python/xbox360_platform/build_policy.py`: Xenon C++ project settings.
- `python/xbox360_platform/contract.py`: checkout and content verification.
- `baseline.json`: SHA-256 manifest of the files in `python/`, `sources/` and selected `third_party/` inputs.

The C++ files are Xbox-specific, not a portable C++ API. A consumer must
provide the declared adapters used by its selected files and check them against
its pinned upstream version. See [BASELINE.md](BASELINE.md) for the ownership
boundary.

[NOTICE.md](NOTICE.md) records the known source lineage and licensing status.

## Using a checkout

Verification requires Python 3.9 or newer and Git.

Clone this repository wherever the consumer can reach it. The consumer resolves
the checkout path through its own configuration and passes that path and its lock
file to `xbox360_platform.contract.verify(base, lock)` before using package
files. Add this repository's `python/` directory to Python's import path for
that call.

The lock file supplies `git_revision` (a Git HEAD prefix of at least seven hex
characters), `api_version`, and `baseline_sha256` (the SHA-256 of the complete
`baseline.json` file). Verification requires the checkout's Git root and checks
HEAD, the manifest digest, the API version, and every file listed in the
manifest. A dirty checkout is accepted while those recorded files still match.
The consumer is responsible for selecting its checkout path and checking any
files or upstream contracts outside this manifest.

When production files change, update `baseline.json` and run the package tests.
After committing the package, update each consumer's revision and manifest hash
in its lock file. An interface change also requires a coordinated
`api_version` change and consumer adapter updates.

Run the package checks from this repository's root:

```sh
python -m unittest discover -s tests -v
```

These checks verify the package contents and lock behavior. PPC compilation and
runtime behavior must be checked by each consumer.
