import unittest
import os
import subprocess
import tempfile
from test_draw_preparation import function
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class NativeTimingTests(unittest.TestCase):
    def test_clock_calls_and_accumulation(self):
        source = (ROOT/'sources/xenos/core.cpp').read_text()
        header = (ROOT/'sources/xenos/core.h').read_text()
        definition = function(header, 'struct NativeTimingCounters {') + ';'
        start = source.index('ShipyardXenos::NativeTimingCounters timing_counts;')
        end = source.index('struct TextureSampler', start)
        implementation = source[start:end] + function(source, 'void SetTimingEnabled(bool enabled) {')
        capture = function(source, 'void CaptureAndResetTimingCounters(NativeTimingCounters* counters) {')
        program = r"""
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <vector>
#define NULL 0
struct LARGE_INTEGER { long long QuadPart; };
unsigned clocks, frequencies;
void QueryPerformanceCounter(LARGE_INTEGER* p) { p->QuadPart = ++clocks * 10; }
void QueryPerformanceFrequency(LARGE_INTEGER* p) { ++frequencies; p->QuadPart = 1000; }
namespace ShipyardXenos { HEADER }
IMPLEMENTATION
using ShipyardXenos::NativeTimingCounters;
struct Framebuffer { unsigned width,height,samples; std::vector<int> tiles; };
std::vector<Framebuffer> buffers;
int active_fb = -1;
typedef unsigned DWORD;
enum { D3DRS_BUFFER2FRAMES, D3DRS_PRESENTINTERVAL };
struct Device { void GetRenderState(int state,DWORD* value) { *value = state ? 1 : 0; } };
Device* xenos_device;
struct D3DSURFACE_DESC { unsigned Width,Height; };
#define SUCCEEDED(x) ((x)>=0)
struct Surface { int GetDesc(D3DSURFACE_DESC* p) { p->Width=1280;p->Height=720;return 0; } };
Surface* presentation_color;
CAPTURE
int main() {
    { NativeTimingScope s(timing_counts.drawTicks, timing_counts.drawCalls); }
    assert(!clocks && !frequencies && !timing_counts.drawCalls);
    SetTimingEnabled(true);
    { NativeTimingScope s(timing_counts.drawTicks, timing_counts.drawCalls); }
    SetTimingEnabled(true);
    { NativeTimingScope s(timing_counts.drawTicks, timing_counts.drawCalls); }
    assert(clocks == 4 && frequencies == 1);
    assert(timing_counts.drawTicks == 20 && timing_counts.drawCalls == 2);
    timing_counts.depthBusyCalls = 3;
    timing_counts.depthIdleCalls = 2;
    timing_counts.depthBusy1080Calls = 2;
    timing_counts.depthIdle1080Calls = 1;
    NativeTimingCounters snapshot;
    Surface surface; presentation_color=&surface;
    Device device; xenos_device=&device;
    Framebuffer fb={};fb.width=640;fb.height=480;fb.samples=4;fb.tiles.resize(2);
    buffers.push_back(fb);active_fb=0;
    CaptureAndResetTimingCounters(&snapshot);
    assert(snapshot.drawCalls==2 && snapshot.drawTicks==20 && snapshot.enabled);
    assert(snapshot.depthBusyCalls==3 && snapshot.depthIdleCalls==2);
    assert(snapshot.depthBusy1080Calls==2 && snapshot.depthIdle1080Calls==1);
    assert(snapshot.frequency==1000 && snapshot.activeWidth==640 && snapshot.activeSamples==4);
    assert(snapshot.activeTiles==2 && snapshot.presentationWidth==1280 && snapshot.presentationHeight==720);
    assert(snapshot.buffer2Frames==0 && snapshot.presentationInterval==1);
    assert(clocks==4 && !timing_counts.drawCalls && !timing_counts.depthBusyCalls);
    CaptureAndResetTimingCounters(&snapshot);
    assert(snapshot.drawCalls==0 && snapshot.frequency==1000 && clocks==4);
    SetTimingEnabled(false);
    { NativeTimingScope s(timing_counts.drawTicks, timing_counts.drawCalls); }
    assert(clocks == 4 && !timing_counts.drawCalls);
    SetTimingEnabled(true);
    assert(!timing_counts.drawTicks && !timing_counts.drawCalls && frequencies == 1);
}
""".replace('HEADER', definition).replace('IMPLEMENTATION', implementation).replace('CAPTURE', capture)
        vswhere = Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
        installation = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
            'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
        with tempfile.TemporaryDirectory(prefix='shipyard-timing-') as folder:
            out = Path(folder)
            (out/'test.cpp').write_text(program)
            batch = out/'compile.cmd'
            batch.write_text('@echo off\ncall "'+installation+'/VC/Auxiliary/Build/vcvars32.bat" >nul\n'
                'if errorlevel 1 exit /b 1\ncl /nologo /EHsc /MT test.cpp /Fecontract.exe\nexit /b %errorlevel%\n')
            built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)], cwd=out, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            result = subprocess.run([str(out/'contract.exe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)

    def test_disabled_clock_and_interval_boundaries(self):
        source = (ROOT/'sources/xenos/core.cpp').read_text()
        self.assertIn('if (timing_enabled) {\n            QueryPerformanceCounter', source)
        self.assertIn('if (enabled == timing_enabled)\n        return;', source)
        self.assertIn('memset(&timing_counts, 0, sizeof(timing_counts));', source)
        self.assertIn('++timing_counts.preparationRebuilt;', source)
        self.assertIn('++timing_counts.preparationReused;', source)
        self.assertEqual(source.count('QueryPerformanceCounter('), 2)
        self.assertEqual(source.count('QueryPerformanceFrequency('), 1)

    def test_depth_busy_probe_is_profile_only_and_precedes_lock(self):
        source = (ROOT/'sources/xenos/core.cpp').read_text()
        body = function(source, 'PixelDepth(int id, const std::set<std::pair<float, float> >& coords) {')
        probe = body.index('depth_texture->IsBusy()')
        guard = body.rfind('if (timing_enabled)', 0, probe)
        lock = body.index('depth_texture->LockRect(')
        self.assertGreater(guard, body.index('if (!b.depth_stored)'))
        self.assertLess(probe, lock)
        self.assertEqual(body.count('depth_texture->IsBusy()'), 1)
        self.assertIn('timing_counts.depthBusy1080Calls', body[guard:lock])
        self.assertIn('timing_counts.depthIdle1080Calls', body[guard:lock])

if __name__ == '__main__':
    unittest.main()
