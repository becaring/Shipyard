import os
from pathlib import Path
import runpy
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
from xbox360_platform import lus_config

CONFIG = """#include "ship/config/Config.h"
namespace Ship {
Config::~Config() {
    SPDLOG_TRACE("destruct config");
}
void Config::Reload() {
    mNestedJson = nlohmann::json::parse(ifs);
}
%s
}
"""
LEAF_SAVE = lus_config.MATCH % lus_config.LEAF
PLAIN_SAVE = lus_config.MATCH % ""


class LusConfigTests(unittest.TestCase):
    def test_nested_save_keeps_nested_json_and_queues_dump(self):
        for save in (PLAIN_SAVE, LEAF_SAVE):
            text = lus_config.background_save(CONFIG % save)
            body = text[text.index("void Config::Save() {"):]
            body = body[:body.index("\n}") + 2]
            self.assertIn("mNestedJson = mFlattenedJson.unflatten();", body)
            self.assertEqual("mFlatLeafSafe" in body, save is LEAF_SAVE)
            self.assertIn("sShipyardConfigWriter.Queue(mPath, mNestedJson, false);", body)
            self.assertNotIn("WriteAtomicFile", body)
            self.assertIn("void Config::Reload() {\n    sShipyardConfigWriter.Wait();", text)
            self.assertLess(text.index("class ShipyardConfigWriter"), text.index("Config::~Config() {"))
            self.assertIn("Config::~Config() {\n    sShipyardConfigWriter.Wait();", text)
            self.assertLess(text.index('#include "async/native.h"'), text.index("namespace Ship {"))

    def test_flat_save_moves_unflatten_to_writer(self):
        smash = "void Config::Save() {\n    std::ofstream file(mPath);\n}"
        text = lus_config.background_save(CONFIG % smash, nested=False, save=smash,
                                          path="fs::absolute(mPath).string()")
        body = text[text.index("void Config::Save() {"):]
        body = body[:body.index("\n}") + 2]
        self.assertNotIn("unflatten", body)
        self.assertIn("Queue(fs::absolute(mPath).string(), mFlattenedJson, true);", body)

    def test_drift_fails_closed(self):
        with self.assertRaises(ValueError):
            lus_config.background_save(CONFIG % "void Config::Save() {\n}")
        with self.assertRaises(ValueError):
            lus_config.background_save((CONFIG % PLAIN_SAVE).replace("void Config::Reload", "void Config::Load"))

    def test_cvar_header_accepts_both_upstream_maps(self):
        from xbox360_platform import console_variable_lookup
        for container in ("std::unordered_map<std::string, std::shared_ptr<CVar>>",
                          "std::unordered_map<std::string, std::shared_ptr<CVar>, TransparentStringHash, "
                          "TransparentStringEqual>"):
            text = console_variable_lookup.header("class ConsoleVariable {\n    " + container + " mVariables;\n};\n")
            self.assertIn("NameKey, std::shared_ptr<CVar>, Shipyard::ConsoleVariables::NameHash> mVariables;", text)
        with self.assertRaises(ValueError):
            console_variable_lookup.header("class ConsoleVariable {\n};\n")

    def test_writer_host_contract(self):
        vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
        installation = subprocess.check_output([
            str(vswhere), "-latest", "-products", "*", "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix="shipyard-config-writer-") as folder:
            out = Path(folder)
            runpy.run_path(str(ROOT / "python/xbox360_platform/json/backend.py"))["generate"](out / "json/generated")
            (out / "config_writer.inc").write_text(lus_config.WRITER)
            batch = out / "compile.cmd"
            batch.write_text(
                '@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\n'
                'cl /nologo /std:c++17 /EHsc /MT /I"' + str(ROOT / "sources/json/include") + '" /I"' +
                str(ROOT / "sources") + '" /I"' + str(out) + '" "' + str(ROOT / "sources/json/json.cpp") +
                '" "' + str(ROOT / "sources/async/native.cpp") + '" "' +
                str(ROOT / "tests/config_writer_contract.cpp") + '" /Fe"' + str(out / "contract.exe") +
                '"\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ["ComSpec"], "/d", "/c", str(batch)],
                                   cwd=out, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(out / "contract.exe")], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Config writer contract passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
