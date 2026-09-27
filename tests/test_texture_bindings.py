import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class TextureBindingTests(unittest.TestCase):
    def test_upload_has_one_native_binding_pass(self):
        source = (ROOT / 'sources/xenos/core.cpp').read_text()
        upload = source[source.index('void Upload(const uint8_t* bytes,'):source.index('\nvoid Sampler(', source.index('void Upload(const uint8_t* bytes,'))]
        self.assertEqual(upload.count('RebindSampledTextures();'), 2)
        self.assertEqual(upload.count('selected_fb[upload_tile] = -1;'), 2)
        self.assertEqual(upload.count('SelectTexture('), 0,
                         'SelectTexture binds the upload slot again before RebindSampledTextures')

    def test_rebind_restores_sampler_after_native_texture(self):
        source = (ROOT / 'sources/xenos/core.cpp').read_text()
        rebind = source[source.index('void RebindSampledTextures() {'):source.index('\nvoid FinishFramebuffer(', source.index('void RebindSampledTextures() {'))]
        self.assertLess(rebind.index('SetNativeTexture(i, texture);'),
                        rebind.index('ApplySampler(i, *sampler);'))
        upload = source[source.index('void Upload(const uint8_t* bytes,'):source.index('\nvoid Sampler(', source.index('void Upload(const uint8_t* bytes,'))]
        self.assertIn('native_state.sampler_valid[i] = false;', upload)

    def test_six_slot_sampler_alias_and_invalidation(self):
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([
            str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'
        ], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-texture-bindings-') as folder:
            output = Path(folder)
            batch = output / 'compile.cmd'
            batch.write_text(
                '@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                'if errorlevel 1 exit /b 1\n'
                'cl /nologo /EHsc /MT /I"' + str(ROOT / 'sources') + '" "' +
                str(ROOT / 'tests/texture_bindings_contract.cpp') + '" /Fe"' +
                str(output / 'contract.exe') + '"\nexit /b %errorlevel%\n'
            )
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)],
                                   cwd=output, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(output / 'contract.exe')], capture_output=True,
                                    text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('six-slot binding and sampler certificates', result.stdout)
