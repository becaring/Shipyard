import hashlib
import json
import sys
import unittest
from pathlib import Path


BASE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(BASE / "python"))
from xbox360_platform import API_VERSION

PRODUCTION = (BASE / "python", BASE / "sources", BASE / "third_party")
TEXT_FILES = {".py", ".cpp", ".h", ".hpp", ".inc", ".md", ".json"}


class BaselineTests(unittest.TestCase):
    def test_manifest_covers_production_files_and_matches_bytes(self):
        manifest = json.loads((BASE / "baseline.json").read_text(encoding="utf-8"))
        self.assertEqual(manifest["name"], "Shipyard")
        self.assertEqual(manifest["api_version"], API_VERSION)
        recorded = manifest["files"]
        self.assertIsInstance(recorded, dict)
        self.assertTrue(recorded)

        actual = {
            path.relative_to(BASE).as_posix()
            for directory in PRODUCTION
            for path in directory.rglob("*")
            if path.is_file() and "__pycache__" not in path.parts
        }
        self.assertEqual(set(recorded), actual)

        for relative, expected in recorded.items():
            with self.subTest(relative=relative):
                self.assertEqual(Path(relative).as_posix(), relative)
                self.assertFalse(Path(relative).is_absolute())
                self.assertRegex(expected, r"^[0-9a-f]{64}$")
                path = (BASE / relative).resolve()
                self.assertTrue(path.is_relative_to(BASE.resolve()))
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), expected)

    def test_text_is_utf8_lf_with_final_newline(self):
        for path in BASE.rglob("*"):
            if not path.is_file() or ".git" in path.parts or "__pycache__" in path.parts:
                continue
            if path.suffix not in TEXT_FILES and path.name not in {".gitattributes", ".gitignore"}:
                continue
            with self.subTest(path=path.relative_to(BASE)):
                data = path.read_bytes()
                self.assertFalse(data.startswith(b"\xef\xbb\xbf"))
                self.assertNotIn(b"\r", data)
                self.assertTrue(data.endswith(b"\n"))
                data.decode("utf-8")


if __name__ == "__main__":
    unittest.main()
