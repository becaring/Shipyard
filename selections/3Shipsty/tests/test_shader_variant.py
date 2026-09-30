"""Exercise the consumer adapter with the shared live-program variant helper."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--modern-root',type=Path,required=True)
    args=parser.parse_args()
    adapter=(args.modern_root/'platform/shipyard/xenos/api.cpp').read_text()
    start=adapter.index('bool shaderSrgb=false;')
    end=adapter.index('\n}\nvoid InitializeNativeDisplay',start)
    body=adapter[start:end]
    header=(args.modern_root/'platform/shipyard/xenos/shader_provider.h').read_text()
    start=header.index('struct ShaderProgramKey {')
    key=header[start:header.index('\n};',start)+3]
    text=r'''
#include "xenos/shader_variant.h"
#include <cassert>
#include <cstdint>
#include <map>
#include <cstdio>
enum FilteringMode { Point, Linear, ThreePoint };
namespace Fast {
''' + key + r'''
struct ShaderProgram { ShaderProgramKey key; };
namespace ShipyardXenos {
std::map<ShaderProgramKey,ShaderProgram*> programs;
FilteringMode filter=Point;int lookups=0,creates=0;
FilteringMode GetTextureFilter(){return filter;}
ShaderProgramKey ShaderKey(const ShaderProgram* program){return program->key;}
ShaderProgram* LookupShader(const ShaderProgramKey& key){++lookups;auto it=programs.find(key);return it==programs.end()?NULL:it->second;}
ShaderProgram* CreateShader(const ShaderProgramKey& key){++creates;auto p=new ShaderProgram{key};programs[key]=p;return p;}
void Shutdown(){for(auto& entry:programs)delete entry.second;programs.clear();}
}
''' + body + r'''
}
int main(){
 using namespace Fast;using namespace Fast::ShipyardXenos;
 assert(CurrentVariant(NULL)==NULL && lookups==0 && creates==0);
 ShaderProgram* point=CreateShader(CurrentKey(12,34));creates=0;
 assert(CurrentVariant(point)==point && lookups==0 && creates==0);
 filter=Linear;ShaderProgram* linear=CurrentVariant(point);
 assert(linear!=point && lookups==1 && creates==1);
 assert(CurrentVariant(linear)==linear && lookups==1 && creates==1);
 assert(CurrentVariant(point)==linear && lookups==2 && creates==1);
 filter=Point;assert(CurrentVariant(point)==point && lookups==2 && creates==1);
 shaderSrgb=true;ShaderProgram* srgb=CurrentVariant(point);
 assert(srgb!=point && lookups==3 && creates==2);
 assert(CurrentVariant(srgb)==srgb && lookups==3 && creates==2);
 shaderSrgb=false;assert(CurrentVariant(srgb)==point && lookups==4 && creates==2);
 // No pointer/cache is retained by the helper across the renderer's lifetime.
 Shutdown();point=CreateShader(CurrentKey(12,34));lookups=creates=0;
 assert(CurrentVariant(point)==point && lookups==0 && creates==0);Shutdown();
 puts("Shader variant identity skips lookup; filter/sRGB transitions and fresh lifetime retain lookup/create");
}
'''
    vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
    installation=subprocess.check_output([str(vswhere),'-latest','-products','*','-requires',
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
    with tempfile.TemporaryDirectory(prefix='shipyard-shader-variant-') as folder:
        output=Path(folder);(output/'contract.cpp').write_text(text)
        batch=output/'compile.cmd'
        batch.write_text('@echo off\ncall "'+installation+'/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
            'if errorlevel 1 exit /b 1\ncl /nologo /std:c++20 /EHsc /MT /I"'+str(ROOT/'sources')+'" contract.cpp /Fecontract.exe\nexit /b %errorlevel%\n')
        built=subprocess.run([os.environ['ComSpec'],'/d','/c',str(batch)],cwd=output,capture_output=True,text=True)
        assert built.returncode==0,built.stdout+built.stderr
        result=subprocess.run([str(output/'contract.exe')],capture_output=True,text=True)
        assert result.returncode==0,result.stdout+result.stderr
        print(result.stdout.strip())


if __name__=='__main__':main()
