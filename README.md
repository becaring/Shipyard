# Shipyard

Shipyard is the shared Xbox 360 platform checkout for LibUltraShip/Fast3D
consumers. Consumers compile its reusable C++ implementations directly and
provide narrow interfaces for game/version-specific behavior. Fast3D formulas,
layout, lifecycle and version behavior stay in the consumers where their
contracts differ; see their ownership reports for the exact boundaries.
Shipyard requires
the consuming port's upstream headers and Xbox 360 SDK and does not produce
an XEX on its own.

## Contents

- `sources/xenos/`: directly compiled renderer, framebuffer and optional shader cache.
- `sources/graphics/`: directly compiled shader lowerer and provider orchestration.
- `sources/imgui/`: directly compiled ImGui renderer and shared gamepad bindings.
- `sources/input/`: directly compiled physical XInput backend.
- `sources/async/`: native synchronization, futures, and detached-worker lifetime.
- `sources/audio/`: XAudio2 sink, layout policy boundary and PCM pool.
- `sources/clock/`, `sources/libultra/`, `sources/crt/`, `sources/filesystem/`, `sources/storage/`: native platform services and finite clock waits.
- `sources/environment/`: bounded launch-file reader, parser and immutable environment table; consumers own path and absence policy.
- `sources/diagnostics/`: owned-record queue, worker, backpressure and drain; consumers supply sinks, formatting and worker placement.
- `sources/network/`, `sources/libzip/`, `sources/io/`, `sources/speech/`: optional socket, archive, I/O and speech data/queue services.
- `sources/entropy/`: caller-initialized native byte fill used by archive and utility adapters.
- `sources/binary/`: XDK binarytools compatibility contract.
- `sources/value_types/`, `sources/prism/`: XDK value-type and Prism compatibility contracts.
- `sources/presentation/`, `sources/fast3d/`: shared pacing and bounded interpreter/geometry helpers.
- `sources/atomic/`: native Xenon atomic operations and explicit memory-order policies.
- `sources/math/`: scalar and color conversion shared by UI projections.
- `sources/memory/`: optional saturating address-interval arithmetic.
- `sources/thread-pool/`: support for the bounded BS thread-pool projection.
- `python/xbox360_platform/bs_thread_pool.py`: source-locked BS 4.1.0 lowering.
- `python/xbox360_platform/json/`: source-locked nlohmann 3.12.0 projection.
- `python/xbox360_platform/libzip.py`: source-locked libzip 1.11.4 XDK projection.
- `python/xbox360_platform/logging_core.py`: pinned spdlog compiler lowering with explicit consumer conversion and formatter policies.
- `python/xbox360_platform/value_types.py`: XDK variant projection.
- `python/xbox360_platform/prism.py`: version-constrained Prism compiler lowering.
- `python/xbox360_platform/c89_lower.py`: C89 lowering with consumer-supplied source policy.
- `python/xbox360_platform/vertex_stream.py`: vertex-stream source transform.
- `python/xbox360_platform/cpp_syntax.py`: C++ syntax lowering shared by active Fast3D generators.
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

## Test environments

Run commands from the selected Shipyard checkout root (for this candidate,
`D:\Projects\LibUltraShip+Fast3D360\scratch\queue-completion-worktree`).
Python 3.9+ and Git are enough for the package manifest, exact-revision lock,
and pure projection checks. In PowerShell, set the import path and run that
portable subset with:

```powershell
$env:PYTHONPATH = (Resolve-Path .\python).Path
python -B -m unittest -v tests.test_baseline tests.test_bs_thread_pool tests.test_combiner_lookup_projection tests.test_contract tests.test_cpp_syntax tests.test_logging_provider tests.test_parallel_build tests.test_prism_projector tests.test_triangle_certificate_projection
```

The full discovered suite also compiles and executes host C++ contracts. Run
it from the same checkout root on Windows with `ComSpec`, the Visual Studio
2022 C++ tools, and `cl.exe` available through `vcvars64.bat`:

```powershell
$env:PYTHONPATH = (Resolve-Path .\python).Path
python -B -m unittest discover -s tests -p 'test_*.py' -v
```

Most host tests find Visual Studio using
`%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe` and
require `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`. The material
value-cache test currently calls the Community 2022 `vcvars64.bat` at
`C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build`.
The separate `python tests/combiner_lookup_contract.py` runner calls that
Community edition's `vcvars32.bat`. The diagnostic queue tests compile with
the real Windows thread/event/critical-section implementation through host
MSVC and the small Xbox declaration shim in `tests/io-sdk`; they do not run
on Linux as unmodified tests.

`tests.test_coordinate` additionally compiles a PPC object with
`C:\Program Files (x86)\Microsoft Xbox 360 SDK\bin\win32\cl.exe` and Xbox
headers. The full suite is therefore green only when that XDK and the host
prerequisites above are present. A missing `ProgramFiles(x86)`, `ComSpec`,
`vswhere.exe`, Community `vcvars*.bat`, or XDK compiler means the relevant
native coverage was not executed; it is not evidence of a production failure
or a passing full suite. The portable subset can still be reported on its
own. Consumer PPC compile/link and Xenia/hardware behavior require the
separate locked consumer build and runtime checks.
