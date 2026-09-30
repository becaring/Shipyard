"""Keep owned code readable and limit tracked documentation to the supported guides."""

EXCLUDE = [
    "**/nlohmann/**",
    "**/BS_thread_pool/**",
    "third_party/**",
    "**/upstream/**",
    "soh/**",
    "libultraship/**",
    "mm/**",
    "OTRExporter/**",
    "ZAPDTR/**",
    "tests/bs_thread_pool_upstream.hpp",
]

DEFERRED = [
    "sources/value_types/xbox_compat/optional.h",
]

import fnmatch
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
CODE_SUFFIXES = {".py", ".cpp", ".h", ".c", ".inc", ".hpp", ".cmake"}
MARKDOWN = {
    "README.md",
    "AGENTS.md",
    "docs/ARCHITECTURE.md",
    "docs/UPSTREAM.md",
    "docs/BUILDING.md",
    "NOTICE.md",
    "docs/CONSUMERS.md",
}
LEGAL = {"NOTICE.md"}
LEGAL_TREES = ("provenance/",)
GITHUB = {".github/workflows/host-tests.yml"}
DOC_WORDS = re.compile(r"\b(receipt|certificate|certified|certification|evidence|ledger|handoff)\b", re.IGNORECASE)
DOC_NAMES = re.compile(r"\d{4}[-_]?\d{2}[-_]?\d{2}|HANDOFF|LEDGER|CHECKPOINT|INTEGRATION", re.IGNORECASE)


def git_paths(root, *arguments):
    command = ["git", "-c", "safe.directory=" + str(root), "-C", str(root), "ls-files", "-z", *arguments]
    output = subprocess.run(command, check=True, capture_output=True).stdout
    return {value.decode("utf-8") for value in output.split(b"\0") if value}


def excluded(name):
    return any(
        fnmatch.fnmatchcase(name, pattern)
        or (pattern.startswith("**/") and fnmatch.fnmatchcase(name, pattern[3:]))
        for pattern in EXCLUDE
    )


def problems(root=ROOT):
    root = Path(root)
    tracked = git_paths(root, "--cached")
    pending = git_paths(root, "--others", "--exclude-standard")
    found = []
    ignore = root / ".gitignore"
    patterns = ignore.read_text(encoding="utf-8").splitlines() if ignore.is_file() else []
    for directory in ("archive", "scratch"):
        if not any(line.strip() in {directory + "/", "/" + directory + "/"} for line in patterns):
            found.append(f".gitignore: missing {directory}/")
        found.extend(f"{name}: tracked {directory}/ file" for name in sorted(tracked) if name.startswith(directory + "/"))
    for name in sorted(tracked | pending):
        path = root / name
        if not path.is_file():
            continue
        if name.startswith(".github/") and name not in GITHUB:
            found.append(f"{name}: .github file is not allowed")
        if excluded(name):
            continue
        if path.suffix.lower() in CODE_SUFFIXES and name not in DEFERRED:
            for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
                if len(line) > 160:
                    found.append(f"{name}:{number}: code line has {len(line)} characters (limit 160)")
        if path.suffix.lower() != ".md":
            continue
        legal = name in LEGAL or name.startswith(LEGAL_TREES)
        if name not in MARKDOWN and not legal:
            found.append(f"{name}: Markdown file is not allowed")
            continue
        if legal:
            continue
        if DOC_NAMES.search(path.name):
            found.append(f"{name}: dated or historical document name")
        for number, line in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
            if DOC_WORDS.search(line):
                found.append(f"{name}:{number}: use current-design language")
    return found


def main():
    found = problems()
    for message in found:
        print(message)
    for name in DEFERRED:
        print("DEFERRED " + name)
    print(f"Hygiene: {len(found)} problems; {len(DEFERRED)} compiled files deferred")
    return bool(found)


if __name__ == "__main__":
    sys.exit(main())
