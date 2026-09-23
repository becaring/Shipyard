# Shipyard

Shipyard is the shared Xbox 360 platform checkout for LibUltraShip/Fast3D
consumers. Consumers compile its reusable C++ implementations directly and
provide narrow interfaces for game/version-specific behavior. Some older
renderer and runtime paths are still being migrated; see the active consumer
ownership reports before treating a subsystem as canonical. Shipyard requires
the consuming port's upstream headers and Xbox 360 SDK and does not produce
an XEX on its own.

## Contents

- `sources/xenos/`: renderer and framebuffer migration inputs; direct compilation is pending.
- `sources/graphics/`: directly compiled shader lowerer.
- `sources/imgui/`: directly compiled ImGui renderer and shared gamepad bindings.
- `sources/input/`: directly compiled physical XInput backend.
- `sources/async/`: native synchronization, futures, and detached-worker lifetime.
- `sources/thread-pool/`: support for the bounded BS thread-pool projection.
- `python/xbox360_platform/bs_thread_pool.py`: source-locked BS 4.1.0 lowering.
- `python/xbox360_platform/vertex_stream.py`: vertex-stream source transform.
- `python/xbox360_platform/build_policy.py`: Xenon C++ project settings.
- `python/xbox360_platform/contract.py`: checkout and content verification.
- `baseline.json`: SHA-256 manifest of the files in `python/` and `sources/`.

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
