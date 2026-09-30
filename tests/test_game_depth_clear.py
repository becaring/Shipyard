from xenos_source import core_source
"""Execute deferred game-clear decisions and their production begin/read boundaries."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GameDepthClearTests(unittest.TestCase):
    def test_first_fill_and_observation_fallbacks(self):
        source = core_source(DEFERRED_FRAMEBUFFERS=1)
        helpers = source[source.index('void BeginGameDepthClear() {'):source.index('void Clear(bool color, bool depth) {')]
        resolve = source[source.index('void Resolve(int id) {'):source.index('int NewFramebuffer() {')]
        begin = source[source.index('void BeginFramebuffer() {'):source.index('void RebindSampledTextures() {')]
        finish_prefix = source[source.index('void FinishFramebuffer(bool preserveDepth, bool preserveColor) {'):]
        finish_prefix = finish_prefix[:finish_prefix.index('    NativeTimingScope timing(')]
        program = r'''
#include <assert.h>
#include <vector>
struct Framebuffer { bool depth, stored, depth_stored; unsigned samples; };
std::vector<Framebuffer> buffers(2);
int active_fb=0, pending_game_depth_clear=-1;
bool live_framebuffer=false, color_writes=true;
unsigned framebuffer_clear_count=0, framebuffer_depth_restore_avoided=0;
unsigned framebuffer_restore_avoided=0, restores=0, begins=0, finishes=0;
int image[2]={37,83}, color=0, depth=0;
void ValidateFramebuffer(int id) { assert(id>=0 && id<2); }
void BeginFramebuffer() {
    __BEGIN_PREFIX__
    ++begins;
    color=0; depth=1;
    if(buffers[active_fb].stored) { color=image[active_fb]; ++restores; }
    live_framebuffer=true;
}
__FINISH_PREFIX__
    ++finishes; image[active_fb]=color;
    buffers[active_fb].stored=preserveColor;
    buffers[active_fb].depth_stored=preserveDepth;
    live_framebuffer=false;
}
void FinishFramebuffer() { FinishFramebuffer(true,true); }
void Clear(bool colorClear,bool depthClear) {
    ++framebuffer_clear_count;
    BeginFramebuffer();
    if(colorClear) color=0;
    if(depthClear) depth=1;
}
__HELPERS__
__RESOLVE__
void reset() {
    pending_game_depth_clear=-1; active_fb=0; live_framebuffer=false; color_writes=true;
    for(unsigned i=0;i<2;++i) { buffers[i].depth=true; buffers[i].stored=true;
        buffers[i].depth_stored=true; buffers[i].samples=1; }
    image[0]=37; image[1]=83; color=0; depth=0;
    framebuffer_clear_count=framebuffer_depth_restore_avoided=framebuffer_restore_avoided=0;
    begins=restores=finishes=0;
}
int main() {
    reset(); BeginGameDepthClear();
    assert(!live_framebuffer && begins==0 && image[0]==37);
    assert(!buffers[0].depth_stored && framebuffer_depth_restore_avoided==1);
    assert(TryDiscardGameColor()); BeginFramebuffer(); color=91; FinishFramebuffer();
    assert(image[0]==91 && restores==0 && framebuffer_restore_avoided==1);
    assert(pending_game_depth_clear==-1 && !TryDiscardGameColor());

    reset(); BeginGameDepthClear(); BeginFramebuffer(); // Partial/ordinary first draw.
    assert(color==37 && depth==1 && restores==1 && !TryDiscardGameColor());

    reset(); BeginGameDepthClear(); Resolve(0); // Read before drawing.
    assert(image[0]==37 && restores==1 && finishes==1 && buffers[0].depth_stored);
    assert(pending_game_depth_clear==-1 && !TryDiscardGameColor());

    reset(); BeginGameDepthClear(); FinishFramebuffer(); active_fb=1; // Target switch/copy.
    assert(image[0]==37 && !TryDiscardGameColor());

    reset(); BeginGameDepthClear(); FinishFramebuffer(); // Empty game pass / frame end.
    assert(image[0]==37 && depth==1 && finishes==1);

    reset(); BeginFramebuffer(); depth=9; BeginGameDepthClear(); // Already live must clear now.
    assert(depth==1 && pending_game_depth_clear==-1 && !TryDiscardGameColor());

    reset(); buffers[0].samples=4; BeginGameDepthClear(); assert(!TryDiscardGameColor());
    reset(); color_writes=false; BeginGameDepthClear(); assert(!TryDiscardGameColor());
    reset(); buffers[0].depth=false; BeginGameDepthClear(); assert(live_framebuffer);
    assert(pending_game_depth_clear==-1 && !TryDiscardGameColor());
}
'''
        begin_prefix = begin.split('    NativeTimingScope timing(', 1)[0].split('{', 1)[1]
        program = (program.replace('__BEGIN_PREFIX__', begin_prefix)
                   .replace('__FINISH_PREFIX__', finish_prefix)
                   .replace('__HELPERS__', helpers).replace('__RESOLVE__', resolve))
        vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-game-clear-') as directory:
            out = Path(directory)
            (out / 'test.cpp').write_text(program)
            batch = out / 'compile.cmd'
            batch.write_text('@echo off\ncall "'+installation+'/VC/Auxiliary/Build/vcvars32.bat" >nul\n'
                             'if errorlevel 1 exit /b 1\ncl /nologo /EHsc /MT test.cpp /Fecontract.exe\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)], cwd=out, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            run = subprocess.run([str(out/'contract.exe')], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
        end = source[source.index('void End() {'):source.index('void Finish() {')]
        self.assertIn('live_framebuffer || pending_game_depth_clear == active_fb || !buffers[active_fb].stored', end)


if __name__ == '__main__':
    unittest.main()
