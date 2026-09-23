import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
from xbox360_platform.bs_thread_pool import SOURCE_SHA256, generate


FIXTURE = ROOT / "tests" / "bs_thread_pool_upstream.hpp"
GENERATED_SHA256 = "ed892886f9da1513a145166c81b0168e5c4aaf0e9416ea796dcd1919e4f8cf0a"


class BSThreadPoolTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def test_refuses_source_drift_before_writing_output(self):
        self.assertEqual(hashlib.sha256(FIXTURE.read_bytes()).hexdigest(), SOURCE_SHA256)
        changed = self.root / "changed.hpp"
        changed.write_bytes(FIXTURE.read_bytes().replace(b"@version 4.1.0", b"@version 4.1.1"))
        output = self.root / "output"
        with self.assertRaisesRegex(ValueError, "Refusing changed BS source"):
            generate(changed, output)
        self.assertFalse(output.exists())

    def test_lf_and_crlf_sources_generate_identical_audited_output(self):
        crlf = self.root / "crlf.hpp"
        crlf.write_bytes(FIXTURE.read_bytes().replace(b"\n", b"\r\n"))
        first = self.root / "first"
        second = self.root / "second"
        manifest = generate(FIXTURE, first)
        self.assertEqual(manifest, generate(crlf, second))
        for name in ("BS_thread_pool.hpp", "provenance.json"):
            self.assertEqual((first / name).read_bytes(), (second / name).read_bytes())

        header = (first / "BS_thread_pool.hpp").read_bytes()
        self.assertEqual(hashlib.sha256(header).hexdigest(), GENERATED_SHA256)
        self.assertEqual(json.loads((first / "provenance.json").read_text(encoding="utf-8")), manifest)
        self.assertIn(b"Shipyard::Async::mutex tasks_mutex", header)
        self.assertIn(b"Keep the completion predicate under tasks_mutex", header)
        self.assertNotIn(b"tasks_lock.unlock();\n            if (waiting", header)


if __name__ == "__main__":
    unittest.main()
