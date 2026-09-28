"""Routing changes invalidate views; stable polls allocate nothing and see fresh samples."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from xbox360_platform.controller_routes import project

ROOT = Path(__file__).resolve().parents[1]

class ControllerRoutesTests(unittest.TestCase):
    def test_routes_and_fresh_samples(self):
        original=(ROOT/'tests/fixtures/controller_routes.cpp').read_text()
        split=original.index('namespace Ship {',original.index('namespace Ship {')+1)
        projected=project('/ConnectedPhysicalDeviceManager.h',original[:split])+project('/ConnectedPhysicalDeviceManager.cpp',original[split:])
        harness=r"""
#include <cassert>
#include <cstdlib>
#include <new>
static size_t allocations=0;
void* operator new(size_t size) { ++allocations; void* p=std::malloc(size); if(!p) throw std::bad_alloc(); return p; }
void operator delete(void* p) noexcept { std::free(p); }
int main() {
    Ship::ConnectedPhysicalDeviceManager m;
    for(int i=0;i<4;i++) pads[i].connected=true;
    m.RefreshConnectedSDLGamepads();
    for(int port=0;port<4;port++) {
        const auto& a=m.GetConnectedSDLGamepadsForPort(port);
        assert(a.size()==4);
        const size_t before=allocations;
        for(int n=0;n<20;n++) {
            pads[0].sample=n;
            const auto& b=m.GetConnectedSDLGamepadsForPort(port);
            assert(&a==&b && b.at(0)->sample==n);
        }
        assert(allocations==before);
    }
    m.IgnoreInstanceIdForPort(0,1);
    assert(m.GetConnectedSDLGamepadsForPort(0).size()==3);
    assert(m.GetConnectedSDLGamepadsForPort(1).size()==4);
    m.UnignoreInstanceIdForPort(0,1);
    assert(m.GetConnectedSDLGamepadsForPort(0).size()==4);
    pads[2].connected=false; m.RefreshConnectedSDLGamepads();
    assert(m.GetConnectedSDLGamepadsForPort(0).count(2)==0);
    pads[2].connected=true; m.RefreshConnectedSDLGamepads();
    assert(m.GetConnectedSDLGamepadsForPort(0).count(2)==1);
    for(int i=0;i<4;i++) m.IgnoreInstanceIdForPort(3,i);
    assert(m.GetConnectedSDLGamepadsForPort(3).empty());
    const size_t before=allocations;
    assert(m.GetConnectedSDLGamepadsForPort(3).empty());
    assert(allocations==before);
}
"""
        vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
        vs=subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
        with tempfile.TemporaryDirectory(prefix='controller-routes-') as folder:
            out=Path(folder); (out/'contract.cpp').write_text(projected+harness)
            (out/'compile.cmd').write_text('@echo off\ncall "'+vs+'/VC/Auxiliary/Build/vcvars64.bat" >nul\ncl /nologo /std:c++20 /EHsc /MT contract.cpp /Fecontract.exe\nexit /b %errorlevel%\n')
            result=subprocess.run([os.environ['ComSpec'],'/d','/c',str(out/'compile.cmd')],cwd=out,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(out/'contract.exe')],capture_output=True,text=True,timeout=15)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

if __name__=='__main__': unittest.main()
