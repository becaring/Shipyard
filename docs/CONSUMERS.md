# Consumer integration

Verification requires Python 3.9 or newer and Git. Clone Shipyard at a location
selected by the consumer's configuration, add its `python/` directory to Python's
import path and call `xbox360_platform.contract.verify(base, lock)` before using
package files. The consumer owns checkout discovery and dependency acquisition.

The base must be the Git checkout root. API 7 rejects the retired `selection`
field; no package subdirectory is selected. The lock contains:

- `git_revision`: the complete commit ID selected by this consumer;
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

See [BASELINE.md](../BASELINE.md) for package ownership and
[development](DEVELOPMENT.md) for verification requirements.
