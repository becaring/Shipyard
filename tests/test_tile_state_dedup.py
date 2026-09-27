"""Actual Fast3D tile setters retain output while avoiding redundant dirtiness."""

import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
from xbox360_platform.texture_state import exact_tile_updates


def method(source, name):
    start = source.index('void Interpreter::' + name + '(')
    return source[start:source.index('\n}', start) + 2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--modern-root', type=Path, required=True)
    parser.add_argument('--native-ppc', action='store_true')
    args = parser.parse_args()
    source = (args.modern_root / 'libultraship/src/fast/interpreter.cpp').read_text(encoding='utf-8-sig')
    names = ('GfxDpSetTile', 'GfxDpSetTileSize')
    original = '\n'.join(method(source, name) for name in names)
    projected = exact_tile_updates(original, enabled_expr='trialEnabled',
                                   capture_expr='captureEnabled', observe_expr='ObserveTile')
    assert projected.count('bool xboxSameTileState = false;') == 2
    try:
        exact_tile_updates(projected, enabled_expr='trialEnabled',
                           capture_expr='captureEnabled', observe_expr='ObserveTile')
    except ValueError:
        pass
    else:
        raise AssertionError('A repeated projection must detect drift')

    classes = []
    for name, methods in (('Original', original), ('Projected', projected)):
        declarations = '\n'.join(method(methods, n).split('{', 1)[0].replace('Interpreter::', '') + ';'
                                 for n in names)
        classes.append('namespace ' + name + ''' {
struct Interpreter {
 RDP storage{}; RDP* mRdp=&storage;
 bool trialEnabled=false,captureEnabled=false;
 unsigned observed[2][2]={{0,0},{0,0}};
 unsigned avoidable=0;
 void ObserveTile(unsigned kind,bool same,bool canAvoid){
  ++observed[kind][same?1:0];if(canAvoid)++avoidable;
 }
''' + declarations + '\n};\n' + methods + '\n}\n')
    driver = r'''
#include <cassert>
#include <cstdint>
#include <iostream>
#define G_TX_WRAP 0
#define G_TX_CLAMP 2
#define G_TX_NOMASK 0
struct Tile {
 uint8_t fmt=0,siz=0,cms=0,cmt=0,shifts=0,shiftt=0;
 float uls=0,ult=0,lrs=0,lrt=0;
 uint16_t tmem=0;
 uint32_t line_size_bytes=0;
 uint8_t palette=0,tmem_index=0;
};
struct RDP { Tile texture_tile[8]; bool textures_changed[2]={false,false}; };
''' + '\n'.join(classes) + r'''
static bool equal(const Tile& a,const Tile& b) {
 return a.fmt==b.fmt&&a.siz==b.siz&&a.cms==b.cms&&a.cmt==b.cmt&&
 a.shifts==b.shifts&&a.shiftt==b.shiftt&&a.uls==b.uls&&a.ult==b.ult&&
 a.lrs==b.lrs&&a.lrt==b.lrt&&a.tmem==b.tmem&&
 a.line_size_bytes==b.line_size_bytes&&a.palette==b.palette&&
 a.tmem_index==b.tmem_index;
}
template<class A,class B> void check(A& a,B& b) {
 for(unsigned i=0;i<8;++i)assert(equal(a.storage.texture_tile[i],b.storage.texture_tile[i]));
}
int main() {
 Original::Interpreter eager;
 Projected::Interpreter trial;
 trial.trialEnabled=true;trial.captureEnabled=true;
 // A same SetTile after SetTileSize must retain the four size fields.
 eager.GfxDpSetTile(1,2,4,256,0,3,0,1,2,0,1,3);
 trial.GfxDpSetTile(1,2,4,256,0,3,0,1,2,0,1,3);
 eager.GfxDpSetTileSize(0,100,200,300,400);
 trial.GfxDpSetTileSize(0,100,200,300,400);
 eager.storage.textures_changed[0]=eager.storage.textures_changed[1]=false;
 trial.storage.textures_changed[0]=trial.storage.textures_changed[1]=false;
 eager.GfxDpSetTile(1,2,4,256,0,3,0,1,2,0,1,3);
 trial.GfxDpSetTile(1,2,4,256,0,3,0,1,2,0,1,3);
 check(eager,trial);
 assert(!trial.storage.textures_changed[0]&&!trial.storage.textures_changed[1]);
 assert(trial.storage.texture_tile[0].uls==100);
 // A dirty flag from a texture load must survive an exact repeat.
 trial.storage.textures_changed[1]=true;
 trial.GfxDpSetTileSize(0,100,200,300,400);
 assert(!trial.storage.textures_changed[0]&&trial.storage.textures_changed[1]);
 eager.storage.textures_changed[0]=eager.storage.textures_changed[1]=false;
 trial.storage.textures_changed[0]=trial.storage.textures_changed[1]=false;
 uint32_t seed=0x6272f73u;
 auto next=[&](){seed=seed*1664525u+1013904223u;return seed;};
 unsigned avoided=0,logicalImports=0;
 for(unsigned i=0;i<20000;++i) {
  unsigned tile=next()%8;
  Tile before=trial.storage.texture_tile[tile];
  // Reuse most values, but interleave tile-size and load-history changes.
  if(i%5==0) {
   unsigned f=next()%5,s=next()%4,line=next()%300,tmem=next()%512;
   unsigned p=next()%16,cmt=next()%4,mt=next()%4,st=next()%16;
   unsigned cms=next()%4,ms=next()%4,ss=next()%16;
   eager.GfxDpSetTile(f,s,line,tmem,tile,p,cmt,mt,st,cms,ms,ss);
   trial.GfxDpSetTile(f,s,line,tmem,tile,p,cmt,mt,st,cms,ms,ss);
  } else if(i%5==1) {
   unsigned uls=next()%65536,ult=next()%65536,lrs=next()%65536,lrt=next()%65536;
   eager.GfxDpSetTileSize(tile,uls,ult,lrs,lrt);
   trial.GfxDpSetTileSize(tile,uls,ult,lrs,lrt);
  } else if(i%5==2) {
   // Exact repeats after either changed size history or a simulated texture load.
   auto t=eager.storage.texture_tile[tile];
   eager.GfxDpSetTile(t.fmt,t.siz,t.line_size_bytes/8,t.tmem,tile,t.palette,
                      t.cmt,1,t.shiftt,t.cms,1,t.shifts);
   trial.GfxDpSetTile(t.fmt,t.siz,t.line_size_bytes/8,t.tmem,tile,t.palette,
                      t.cmt,1,t.shiftt,t.cms,1,t.shifts);
  } else if(i%5==3) {
   auto t=eager.storage.texture_tile[tile];
   eager.GfxDpSetTileSize(tile,(uint16_t)t.uls,(uint16_t)t.ult,
                                (uint16_t)t.lrs,(uint16_t)t.lrt);
   trial.GfxDpSetTileSize(tile,(uint16_t)t.uls,(uint16_t)t.ult,
                                (uint16_t)t.lrs,(uint16_t)t.lrt);
  } else {
   eager.storage.textures_changed[tile&1]=true;
   trial.storage.textures_changed[tile&1]=true;
  }
  check(eager,trial);
  if(!equal(before,trial.storage.texture_tile[tile]))
   assert(trial.storage.textures_changed[0]||trial.storage.textures_changed[1]);
  if(eager.storage.textures_changed[0]||eager.storage.textures_changed[1]) {
   ++logicalImports;
   eager.storage.textures_changed[0]=eager.storage.textures_changed[1]=false;
   trial.storage.textures_changed[0]=trial.storage.textures_changed[1]=false;
  } else if(trial.storage.textures_changed[0]||trial.storage.textures_changed[1]) {
   assert(false);
  }
  avoided+=trial.observed[0][1]+trial.observed[1][1];
  trial.observed[0][1]=trial.observed[1][1]=0;
 }
 assert(avoided>1000&&trial.avoidable>1000&&logicalImports>0);
 // Disabled trial must retain the original dirty transitions exactly.
 trial.trialEnabled=false;
 for(unsigned i=0;i<8;++i) {
  eager.GfxDpSetTileSize(i,1,2,3,4);
  trial.GfxDpSetTileSize(i,1,2,3,4);
  assert(eager.storage.textures_changed[0]==trial.storage.textures_changed[0]);
  assert(eager.storage.textures_changed[1]==trial.storage.textures_changed[1]);
 }
 std::cout<<"Tile output matches over 20,000 transitions; same-state observations "<<avoided<<"\n";
}
'''
    vswhere = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    installation = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
    with tempfile.TemporaryDirectory(prefix='shipyard-tile-state-') as folder:
        output = Path(folder)
        (output / 'contract.cpp').write_text(driver)
        batch = output / 'compile.cmd'
        batch.write_text('@echo off\ncall "' + installation + '/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
                         'if errorlevel 1 exit /b 1\ncl /nologo /std:c++20 /EHsc /MT contract.cpp /Fecontract.exe\n'
                         'exit /b %errorlevel%\n')
        built = subprocess.run([os.environ['ComSpec'], '/d', '/c', str(batch)], cwd=output,
                               capture_output=True, text=True)
        assert built.returncode == 0, built.stdout + built.stderr
        result = subprocess.run([str(output / 'contract.exe')], capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr
        print(result.stdout.strip())
        if args.native_ppc:
            ppc = '''typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
#define G_TX_WRAP 0
#define G_TX_CLAMP 2
#define G_TX_NOMASK 0
struct Tile {
 uint8_t fmt,siz,cms,cmt,shifts,shiftt;
 float uls,ult,lrs,lrt;
 uint16_t tmem;
 uint32_t line_size_bytes;
 uint8_t palette,tmem_index;
};
struct RDP { Tile texture_tile[8]; bool textures_changed[2]; };
namespace Projected {
struct Interpreter {
 RDP* mRdp; bool trialEnabled,captureEnabled;
 void ObserveTile(unsigned,bool,bool) {}
''' + '\n'.join(method(projected, n).split('{', 1)[0].replace('Interpreter::', '') + ';'
                      for n in names) + '\n};\n' + projected + '\n}\n'
            fixture = output / 'tile_ppc.cpp'
            fixture.write_text(ppc)
            obj = output / 'tile_ppc.obj'
            compiler = Path('C:/Program Files (x86)/Microsoft Xbox 360 SDK/bin/win32/cl.exe')
            built = subprocess.run([str(compiler), '/nologo', '/c', '/O2', '/Fo' + str(obj),
                                    str(fixture)], cwd=output, capture_output=True, text=True)
            assert built.returncode == 0, built.stdout + built.stderr
            assert obj.read_bytes()[:2] == b'\xf2\x01', 'Expected PowerPC big-endian COFF object'
            print('Native PowerPC tile setters compile')


if __name__ == '__main__':
    main()
