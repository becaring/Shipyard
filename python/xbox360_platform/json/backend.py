"""Generate the pinned XDK JSON algorithm and ordered DOM sources."""

import hashlib
import json
from pathlib import Path
import runpy


BASE = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
SOURCE = BASE / "third_party/nlohmann-3.12.0"


def generate(output: Path, source: Path = SOURCE) -> dict:
    source = Path(source).resolve()
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)

    algorithms = runpy.run_path(str(HERE / "generate_algorithms.py"))["generate"](source, output)
    streams = runpy.run_path(str(HERE / "generate_streams.py"))["generate"](source, output)
    ordered = runpy.run_path(str(HERE / "generate_ordered.py"))["generate"](source, output)
    inputs = {
        path.relative_to(source).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted((source / "include/nlohmann").rglob("*"))
        if path.is_file()
    }
    result = {
        "upstream_version": "3.12.0",
        "upstream_commit": "55f93686c01528224f448c19128836e7df245f72",
        "upstream_inputs": inputs,
        "algorithms": algorithms,
        "streams": streams,
        "ordered": ordered,
    }
    (output / "shipyard_manifest.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result
