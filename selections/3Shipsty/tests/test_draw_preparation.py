import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    while ';' in source[start:brace]:
        start = source.index(signature, start + len(signature))
        brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class DrawPreparationTests(unittest.TestCase):
    def test_original_differential_and_preparation_counts(self):
        source = (ROOT/'sources/xenos/core.cpp').read_text()
        if 'void PrepareDrawState() {' in source:
            methods = function(source, 'void RebuildDrawState() {') + '\n' + function(source, 'void PrepareDrawState() {')
            methods = function(source, 'void VerifyPreparedDrawState() {') + '\n' + methods
        else:
            start = source.index('    __declspec(align(16)) float constants[12] =', source.index('void Draw('))
            end = source.index('    ApplyAlpha();', start)
            methods = 'void PrepareDrawState() {\n' + source[start:end] + '}\n'
        for signature in ['void InvalidateNativeState() {', 'uint32_t NewTexture() {',
                          'void SelectTexture(int tile, uint32_t id) {',
                          'void Sampler(int tile, bool linear, uint32_t s, uint32_t t) {',
                          'void Filter(FilteringMode mode) {', 'void BindFramebuffer(int id, float scale) {',
                          'void RebindSampledTextures() {', 'void SelectFramebuffer(int id) {',
                          'void DeleteTexture(uint32_t id) {', 'void LoadShader(ShaderProgram* shader) {']:
            methods += '\n' + function(source, signature) + '\n'
        vswhere = Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-draw-preparation-') as folder:
            out = Path(folder)
            (out/'production.inc').write_text(methods)
            batch = out/'compile.cmd'
            batch.write_text('@echo off\ncall "'+installation+'/VC/Auxiliary/Build/vcvars32.bat" >nul\n'
                'if errorlevel 1 exit /b 1\ncl /nologo /EHsc /MT /I"'+str(out)+'" /I"'+str(ROOT/'tests')+'" /I"'+str(ROOT/'sources')+'" "'+
                str(ROOT/'tests/draw_preparation_contract.cpp')+'" /Fe"'+str(out/'contract.exe')+'"\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)], cwd=out, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            result = subprocess.run([str(out/'contract.exe')], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            print(result.stdout.strip())

    def test_complete_mutation_and_external_boundaries(self):
        source = (ROOT/'sources/xenos/core.cpp').read_text()
        for signature in ['void InvalidateNativeState()', 'void ApplySamplerSlot(',
                          'uint32_t NewTexture()', 'void SelectTexture(int tile,',
                          'void RebindSampledTextures()', 'void DeleteTexture(uint32_t id)',
                          'void LoadShader(ShaderProgram* shader)', 'void Filter(FilteringMode mode)',
                          'void BindFramebuffer(int id, float scale)']:
            self.assertIn('draw_state_prepared = false;', function(source, signature), signature)
        for signature in ['void UpdateFramebuffer(', 'void DestroyFramebufferNative(',
                          'void Start()', 'void Init()', 'void Shipyard_ShutdownGraphics()',
                          'bool BeginRestoreQuad(', 'void EndRestoreQuad(', 'void FinishFramebuffer(bool preserveDepth, bool preserveColor)', 'void InvalidateGuiState()']:
            self.assertIn('InvalidateNativeState();', function(source, signature), signature)
        dispatch = function(source, 'void ApplySampler(int tile,')
        for slot in range(6):
            self.assertIn('ApplySamplerSlot<' + str(slot) + '>(sampler);', dispatch)
        restore = function(source, 'void RestoreQuad(')
        self.assertIn('BeginRestoreQuad(width, height, depth != NULL);', restore)
        self.assertIn('EndRestoreQuad(restore_constants);', restore)
        upload = function(source, 'void Upload(const uint8_t* bytes,')
        self.assertEqual(upload.count('RebindSampledTextures();'), 2)
        self.assertIn('ApplySampler(0, buffers[id].sampler);', function(source, 'void SelectFramebuffer('))
        draw = function(source, 'void Draw(float* vertices,')
        self.assertLess(draw.index('BeginFramebuffer();'), draw.index('PrepareDrawState();'))

if __name__ == '__main__':
    unittest.main()
