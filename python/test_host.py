"""Run portable package checks without requiring an Xbox SDK or host C++ compiler."""

from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
TESTS = [
    "tests.test_baseline",
    "tests.test_bs_thread_pool",
    "tests.test_combiner_lookup_projection",
    "tests.test_contract",
    "tests.test_cpp_syntax",
    "tests.test_hygiene",
    "tests.test_logging_provider",
    "tests.test_parallel_build",
    "tests.test_prism_projector",
    "tests.test_triangle_certificate_projection",
]


def main():
    import os

    environment = os.environ.copy()
    environment["PYTHONPATH"] = str(ROOT / "python")
    subprocess.run([sys.executable, "-B", str(ROOT / "python/check_hygiene.py")], cwd=ROOT, check=True)
    subprocess.run([sys.executable, "-B", "-m", "unittest", "-v", *TESTS], cwd=ROOT, env=environment, check=True)


if __name__ == "__main__":
    main()
