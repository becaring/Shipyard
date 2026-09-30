"""A1: execute projected directory commands without storing unused paths."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
from xbox360_platform import fast3d_opcode

SOURCE = r"""std::stack<std::string> currentDir;
static std::string GetPathWithoutFileName(char* filePath) {
    return std::string(filePath).substr(0, std::string(filePath).find_last_of("/"));
}
void reset() {
    currentDir = std::stack<std::string>();
}
void gfx_push_current_dir(char* path) {
    if (gfx_check_image_signature(path) == 1)
        path = &path[7];
    currentDir.push(GetPathWithoutFileName(path));
}
bool handler(int*& command, char* path) {
    gfx_push_current_dir(path);
    ++command;
    return false;
}
"""


class DirectoryTests(unittest.TestCase):
    def test_command_keeps_consumption_without_directory_allocations(self):
        self.assertTrue(hasattr(fast3d_opcode, "remove_unused_directory"), "shared A1 lowering is missing")
        projected = fast3d_opcode.remove_unused_directory(SOURCE)
        vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
        vs = subprocess.check_output(
            [
                str(vswhere),
                "-latest",
                "-products",
                "*",
                "-requires",
                "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                "-property",
                "installationPath",
            ],
            text=True,
        ).strip()
        prefix = r"""#include <stack>
#include <string>
#include <cstdlib>
#include <new>
static unsigned allocations, checks;
void* operator new(size_t n) { ++allocations; void* p=malloc(n); if(!p)throw std::bad_alloc(); return p; }
void operator delete(void* p) noexcept { free(p); }
int gfx_check_image_signature(const char*) { ++checks; return 1; }
"""
        main = r"""int main() {
    int words[2]={0}; int* command=words;
    char path[]="__OTR__a/very/long/directory/path/which/exceeds/any/small/string/buffer/resource";
    allocations=checks=0;
    bool branch=handler(command,path);
    if(branch || command!=words+1 || checks!=1) return 2;
    if(allocations!=0) return 3;
    reset(); return 0;
}
"""
        with tempfile.TemporaryDirectory(prefix="shipyard-directory-") as folder:
            out = Path(folder)
            (out / "contract.cpp").write_text(prefix + projected + main)
            (out / "compile.cmd").write_text(
                '@echo off\ncall "'
                + vs
                + '/VC/Auxiliary/Build/vcvars64.bat" >nul\nif errorlevel 1 exit /b 1\ncl /nologo /EHsc /O2 contract.cpp /Fecontract.exe\nexit /b %errorlevel%\n'
            )
            build = subprocess.run(
                [os.environ["ComSpec"], "/d", "/c", str(out / "compile.cmd")], cwd=out, capture_output=True, text=True
            )
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(out / "contract.exe")], cwd=out)
            self.assertEqual(run.returncode, 0, "directory allocation or opcode semantics regressed")

    def test_new_directory_reader_is_rejected(self):
        self.assertTrue(hasattr(fast3d_opcode, "remove_unused_directory"), "shared A1 lowering is missing")
        with self.assertRaises(ValueError):
            fast3d_opcode.remove_unused_directory(SOURCE + "auto read() { return currentDir.top(); }\n")


if __name__ == "__main__":
    unittest.main()
