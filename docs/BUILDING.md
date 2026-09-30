# Building and testing

Shipyard builds as part of a game. Clone this repository as `Shipyard` beside
the game repository, install that game's dependencies, and follow its build
guide. Use `XBOX360_PLATFORM_BASE` for a different checkout location.

## Portable checks

Python 3.9 or newer and Git are required. From the Shipyard checkout:

```powershell
python -B python/check_hygiene.py
python -B python/test_host.py
```

The portable runner checks the manifest, lock contract, hygiene policy, and
compiler projections that do not need a C++ compiler. CI runs this command.

## Full host suite

Install `tree_sitter` and `tree_sitter_cpp` in the Python environment. Native
tests also need Windows, Visual Studio 2022 C++ tools, and the Xbox 360 SDK for
PowerPC compile checks. `vswhere.exe` must find the C++ x86/x64 component. Some
runners use the standard Community 2022 and Xbox SDK installation paths.

```powershell
$env:PYTHONPATH = (Resolve-Path ./python).Path
python -B -m unittest discover -s tests -p 'test_*.py' -v
```

Set `SHIPYARD_NLOHMANN_INCLUDE` to a full nlohmann 3.12.0 include directory to
enable the optional reference-parser test. The package's smaller pinned input
set is sufficient for its projectors.

These tests exercise host behavior and compile selected PowerPC units. They do
not run a game. Runtime checks belong in each game's hardware test routine.

## Editing projectors

Trace the caller and pinned input before changing a projector. Keep numerical,
lifetime, synchronization, and invalidation rules intact. Keep generated files,
disassembly, measurements, and working notes in ignored `scratch/` or `archive/`.

For formatting-only changes, record generated-source hashes, rerun only the
changed projector, and require identical output bytes. Leave compiled sources
listed in the hygiene check's DEFERRED list for work that also rebuilds them.

After changing a packaged file, refresh the `files` mapping in `baseline.json`
with SHA-256 hashes of every file under `python/`, `sources/`, and `third_party/`.
Exclude Python bytecode directories. Run the hygiene check and full suite before
committing; consumers then pin the commit and the manifest's SHA-256 digest.
