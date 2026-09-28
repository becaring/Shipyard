import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class SamplerCertificateTests(unittest.TestCase):
    def test_core_invalidates_only_on_logical_sampler_changes(self):
        source = (ROOT / 'sources/xenos/core.cpp').read_text()
        apply = source.split('void ApplySampler(int tile, const TextureSampler& sampler) {', 1)[1]
        apply = apply.split('\n}', 1)[0]
        self.assertNotIn('draw_state_prepared = false;', apply)
        self.assertIn('sampler_certificate.Update(', source)

    def test_logical_inputs_and_external_invalidation(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        program = r'''
#include <assert.h>
#include "sampler_certificate.h"
using Fast::ShipyardXenos::SamplerCertificate;
int main() {
    SamplerCertificate state = {};
    assert(state.Update(0, false, 0, 0, 0));
    assert(!state.Update(0, false, 0, 0, 0));
    assert(state.Update(0, false, 4, 0, 0)); // Same hardware address bits; different logical input.
    assert(!state.Update(0, false, 4, 0, 0));
    assert(state.Update(0, false, 4, 4, 0));
    assert(state.Update(0, true, 4, 4, 0));
    assert(state.Update(0, true, 4, 4, 1));
    assert(!state.Update(0, true, 4, 4, 1));
    assert(state.Update(1, true, 4, 4, 1)); // Slots are independent even for aliased textures.
    assert(!state.Update(1, true, 4, 4, 1));
    state.Invalidate();
    assert(state.Update(0, true, 4, 4, 1));
    assert(state.Update(1, true, 4, 4, 1));
}
'''
        with tempfile.TemporaryDirectory(prefix='shipyard-sampler-') as folder:
            out = Path(folder)
            (out / 'test.cpp').write_text(program)
            batch = out / 'compile.cmd'
            batch.write_text('@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars32.bat" >nul\n'
                'if errorlevel 1 exit /b 1\ncl /nologo /EHsc /MT /I"' +
                str(ROOT / 'sources/xenos') + '" test.cpp /Fecontract.exe\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)], cwd=out,
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(out / 'contract.exe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
