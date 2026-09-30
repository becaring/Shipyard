# Consumer integration

Verification requires Python 3.9 or newer and Git. Clone Shipyard at a location
selected by the consumer's configuration, add its `python/` directory to Python's
import path and call `xbox360_platform.contract.verify(base, lock)` before using
package files. The consumer owns checkout discovery and dependency acquisition.

The lock contains:

- `git_revision`: the selected Git HEAD, with at least seven hexadecimal characters;
- `api_version`: the package interface version;
- `baseline_sha256`: SHA-256 of the complete `baseline.json` file.

Verification checks the Git root and HEAD, manifest digest, API version and every
manifested file. Uncommitted changes outside the manifest are permitted; they do
not relax verification of production inputs. Consumers also check upstream and
adapter inputs outside this package manifest.

Keep each consumer on its exact selected revision until an intentional adoption.
For a package change, review maintained and generated differences, update the
affected hashes in `baseline.json`, run relevant contracts and commit. Consumers
then update their lock revision and manifest digest, regenerate and validate
their selected build. Interface changes additionally require coordinated API
version and adapter updates. Documentation edits do not change the package API.

Consumers can compile `sources/xenos/core.cpp` with
`SHIPYARD_XENOS_TILING_TRIALS=0` to exclude experimental tiling paths from the
native draw routine. The default is `1`, preserving the runtime trial switches.
An excluded build rejects attempts to enable those switches. Record this flag
in the consumer's selected build recipe; it does not change the renderer ABI.

See [BASELINE.md](../BASELINE.md) for package ownership and
[development](DEVELOPMENT.md) for verification requirements.
