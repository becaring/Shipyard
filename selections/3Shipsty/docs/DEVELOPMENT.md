# Development

Use Python 3.9+ and Git. Run tests from the selected Shipyard checkout; consumer
generation must use the revision recorded in that consumer's lock.

```powershell
$env:PYTHONPATH = (Resolve-Path .\python).Path
python -B -m unittest -v tests.test_baseline tests.test_bs_thread_pool tests.test_combiner_lookup_projection tests.test_contract tests.test_cpp_syntax tests.test_logging_provider tests.test_parallel_build tests.test_prism_projector tests.test_triangle_certificate_projection
```

This portable subset checks manifests, locks and pure projections. The complete
suite also compiles host C++ contracts and selected PowerPC objects:

```powershell
python -B -m unittest discover -s tests -p 'test_*.py' -v
```

Native tests require Windows, `ComSpec`, Visual Studio 2022 C++ tools and the
Xbox 360 SDK for PowerPC coverage. Most host runners locate Visual Studio through
`vswhere.exe` and require `Microsoft.VisualStudio.Component.VC.Tools.x86.x64`.
Some runners currently expect the standard Community 2022 installation and its
`vcvars64.bat` or `vcvars32.bat`; PowerPC runners expect the standard Xbox SDK
installation. Inspect the affected test runner when using another toolchain
layout. These installation locations are test harness constraints, not consumer
source locations. SDK headers and binaries are not included.

Diagnostic queue contracts exercise Windows threads, events and critical
sections with the declarations in `tests/io-sdk`; they are not Linux runtime
tests. A missing compiler means that native coverage was not run. Portable tests
alone do not establish that the full suite or a consumer build passes.

For a maintained change, trace the selected consumer adapter and upstream
contract, edit the source or projector that owns the behavior and inspect emitted
code. Preserve numerical, lifetime, synchronization and invalidation contracts.
Use focused regression tests and affected PowerPC compilation before a coordinated
consumer build. Update only reviewed manifest hashes. Consumer link and runtime
checks remain separate from these package tests.

Keep temporary generated code, disassembly and measurement reports under ignored
`scratch/`. Operation-count tests demonstrate less work for their inputs; they do
not establish frame-rate improvements on hardware.
