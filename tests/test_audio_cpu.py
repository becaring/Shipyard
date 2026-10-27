"""Audio-thread controls freeze per synthesis call; matrix coefficient memo; VMX alias rule."""

import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))
from xbox360_platform import audio_cpu

CONTROLS = (
    ("Master", "float", 'CVarGetFloat("gSettings.Audio.MasterVolume", 1.0f)'),
    ("Mirrored", "int", 'CVarGetInteger("gModes.MirroredWorld.State", 0)'),
)
SYNTHESIS = """\
Acmd* AudioSynth_Update(Acmd* start, s32* count) {
    Acmd* cmd = start;
    if (cmd == NULL) return cmd;
    AudioScript_ProcessSequences(0);
    if (CVarGetInteger("gModes.MirroredWorld.State", 0)) cmd++;
    *count = cmd - start;
    return cmd;
}
"""

CONTRACT = r"""
#include <stdio.h>
static float liveMaster = 0.5f;
static int liveMirror = 0, reads = 0;
static float CVarGetFloat(const char* name, float value) { (void)name; (void)value; ++reads; return liveMaster; }
static int CVarGetInteger(const char* name, int value) { (void)name; (void)value; ++reads; return liveMirror; }
%s
int main(void) {
    int failures = 0;
    if (ShipyardAudioControl_Master() != 0.5f || reads != 1) ++failures;
    ShipyardAudioControls_Begin();
    liveMaster = 1.0f;
    liveMirror = 1;
    reads = 0;
    if (ShipyardAudioControl_Master() != 0.5f || ShipyardAudioControl_Mirrored() != 0 || reads) ++failures;
    ShipyardAudioControls_End();
    if (ShipyardAudioControl_Master() != 1.0f || ShipyardAudioControl_Mirrored() != 1) ++failures;
    if (!failures) printf("Audio controls freeze passed\n");
    return failures;
}
"""


class AudioCpuTests(unittest.TestCase):
    def test_scope_brackets_the_synthesis_call(self):
        text = audio_cpu.read(SYNTHESIS, CONTROLS[1][2], "Mirrored")
        text = audio_cpu.scope(text, "    AudioScript_ProcessSequences(", "    *count = cmd - start;")
        self.assertEqual(text.count("\n"), SYNTHESIS.count("\n"))
        self.assertLess(text.index("return cmd;"), text.index(audio_cpu.BEGIN + "();"))
        self.assertLess(text.index(audio_cpu.BEGIN + "();"), text.index("AudioScript_ProcessSequences"))
        self.assertLess(text.index("ShipyardAudioControl_Mirrored()"), text.index(audio_cpu.END + "();"))
        self.assertLess(text.index(audio_cpu.END + "();"), text.index("*count = cmd - start;"))
        with self.assertRaises(ValueError):
            audio_cpu.read(SYNTHESIS, CONTROLS[0][2], "Master")

    def test_definitions_freeze_on_the_owning_thread(self):
        definitions = audio_cpu.definitions(CONTROLS)
        self.assertEqual(definitions.count("CVarGetInteger("), 2)
        self.assertIn("float ShipyardAudioControl_Master(void);", audio_cpu.declarations(CONTROLS))
        vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio/Installer/vswhere.exe"
        installation = subprocess.check_output([
            str(vswhere), "-latest", "-products", "*", "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix="shipyard-audio-") as folder:
            output = Path(folder)
            (output / "controls.c").write_text(CONTRACT % definitions)
            batch = output / "build.cmd"
            batch.write_text('@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                             'cl /nologo /TC /W4 /WX controls.c /Fecontrols.exe\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ["ComSpec"], "/d", "/c", str(batch)], cwd=output,
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(output / "controls.exe")], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Audio controls freeze passed", result.stdout)

    def test_matrix_coefficient_memo(self):
        text = audio_cpu.matrix("    double c = (1.0 - chain.Freq) / (1.0 + chain.Freq);\n")
        self.assertIn("coefficientFrequency != chain.Freq", text)
        self.assertIn("    double c = coefficientValue;", text)
        with self.assertRaises(ValueError):
            audio_cpu.matrix(text)

    def test_saturated_mix_alias_rule(self):
        # mixer_vmx.h vectorizes only identical or disjoint ranges; model full DMEM
        # with sequential feedback for overlapping ranges.
        rng = random.Random(84)

        def clamp(x):
            return max(-32768, min(32767, x))

        for subtract in (False, True):
            for offset in (0, 1, 7, 8, 16, 40):
                values = [rng.randrange(-32768, 32768) for _ in range(128)]
                expected, actual = values[:], values[:]
                start, destination, count = 32, 32 + offset, 32
                sign = -1 if subtract else 1
                for i in range(count):
                    expected[destination + i] = clamp(expected[destination + i] + sign * expected[start + i])
                width = 8 if offset == 0 or offset >= count else 1
                for i in range(0, count, width):
                    results = [clamp(actual[destination + i + j] + sign * actual[start + i + j])
                               for j in range(width)]
                    actual[destination + i:destination + i + width] = results
                self.assertEqual(expected, actual)


if __name__ == "__main__":
    unittest.main()
