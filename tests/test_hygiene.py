"""Exercise hygiene policy with isolated Git indexes so failures identify one rule."""

import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("hygiene", ROOT / "python/check_hygiene.py")
hygiene = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(hygiene)


class HygieneTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        self.git("init", "-q")
        self.write(".gitignore", "/archive/\n/scratch/\n")

    def git(self, *args):
        return subprocess.run(
            ["git", "-C", str(self.root), *args], check=True, capture_output=True,
        )

    def write(self, name, text, tracked=False):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        if tracked:
            self.git("add", "-f", "--", name)

    def test_boundary_vendor_and_named_deferral(self):
        self.write("owned.py", "x" * 160 + "\n")
        self.write("third_party/vendor/code.h", "x" * 180 + "\n")
        self.write(hygiene.DEFERRED[0], "x" * 180 + "\n")
        self.assertEqual(hygiene.problems(self.root), [])
        self.write("owned.py", "x" * 161 + "\n")
        self.assertEqual(len(hygiene.problems(self.root)), 1)
        self.write("sources/unlisted.h", "x" * 180 + "\n")
        self.assertEqual(len(hygiene.problems(self.root)), 2)

    def test_ignored_work_is_not_tracked_even_for_placeholders(self):
        self.write("scratch/.gitignore", "*\n", tracked=True)
        self.write("archive/notes.txt", "notes\n", tracked=True)
        found = hygiene.problems(self.root)
        self.assertEqual(len(found), 2)
        self.assertTrue(all("tracked" in item for item in found))
        self.write(".gitignore", "")
        self.assertEqual(len(hygiene.problems(self.root)), 4)

    def test_markdown_allowlist_and_legal_exemptions(self):
        self.write("README.md", "Current design\n", tracked=True)
        self.write("NOTICE.md", "Historical evidence\n", tracked=True)
        self.write("provenance/LINEAGE-20260101.md", "Historical evidence\n", tracked=True)
        self.assertEqual(hygiene.problems(self.root), [])
        self.write("module/README.md", "Details\n", tracked=True)
        self.write("README.md", "Certified result\n")
        self.assertEqual(len(hygiene.problems(self.root)), 2)

    def test_github_allowlist(self):
        self.write(".github/workflows/host-tests.yml", "name: Host tests\n", tracked=True)
        self.assertEqual(hygiene.problems(self.root), [])
        self.write(".github/ISSUE_TEMPLATE/bug.yml", "name: Bug\n", tracked=True)
        self.assertEqual(len(hygiene.problems(self.root)), 1)

    def test_checkout_meets_policy(self):
        self.assertEqual(hygiene.problems(ROOT), [])


if __name__ == "__main__":
    unittest.main()
