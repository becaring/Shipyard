"""Compare the pinned matrix operations with lazy materialization and Smash patches."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from xbox360_platform.matrix_product import source
ROOT=Path(__file__).resolve().parents[1]

class MatrixProductTests(unittest.TestCase):
    def test_command_sequences(self):
        original=(ROOT/'tests/fixtures/matrix_product.cpp').read_text()
        projected=source(original)
        common=r"""
struct RSP { float modelview_matrix_stack[11][4][4]; uint8_t modelview_matrix_stack_size;
float MP_matrix[4][4],P_matrix[4][4]; bool xboxMPDirty,lights_changed; };
struct Mtx {}; struct MtxF {float mf[4][4];}; struct F3DVtx {};
enum {MTX_PROJECTION=1,MTX_LOAD=2,MTX_PUSH=4};
int get_attr(int bit){return bit;}
struct Interpreter {
 RSP* mRsp; std::unordered_map<Mtx*,MtxF> replacements; std::unordered_map<Mtx*,MtxF>* mCurMtxReplacements;
 float observed[4][4]; static unsigned products;
 Interpreter(); ~Interpreter(){delete mRsp;}
 static void MatrixMul(float[4][4],const float[4][4],const float[4][4]);
 void EnsureMatrixProduct(); void SpReset(); void GfxSpMatrix(uint8_t,const int32_t*);
 void GfxSpPopMatrix(uint32_t);void GfxSpVertex(size_t,size_t,const F3DVtx*);
 void GfxSpMovewordF3dex2(uint8_t,uint16_t,uintptr_t);
}; unsigned Interpreter::products=0;
"""
        original=original.replace('void Interpreter::MatrixMul(float res[4][4], const float a[4][4], const float b[4][4]) {','void Interpreter::MatrixMul(float res[4][4], const float a[4][4], const float b[4][4]) { ++products;')
        projected=projected.replace('void Interpreter::MatrixMul(float res[4][4], const float a[4][4], const float b[4][4]) {','void Interpreter::MatrixMul(float res[4][4], const float a[4][4], const float b[4][4]) { ++products;')
        program='#include <cassert>\n#include <cstdint>\n#include <cstring>\n#include <cmath>\n#include <unordered_map>\n#define GBI_FLOATS\nnamespace Eager {\n'+common+original+'\n}\nnamespace Lazy {\n'+common+projected+'\n}\n'+r"""
static uint32_t seed=7; static uint32_t random_word(){seed=1664525*seed+1013904223;return seed;}
int main(){
 Eager::Interpreter e; Lazy::Interpreter l;
 e.SpReset();l.SpReset();float a[4][4]={};for(int i=0;i<4;i++)a[i][i]=1;
 e.GfxSpMatrix(3,(int32_t*)a);l.GfxSpMatrix(3,(int32_t*)a);
 e.GfxSpMatrix(2,(int32_t*)a);l.GfxSpMatrix(2,(int32_t*)a);
 for(unsigned n=0;n<20000;n++) {
  unsigned op=random_word()%9;
  if(e.mRsp->modelview_matrix_stack_size==0)op=0;
  if(op<4){
   memset(a,0,sizeof(a));for(int i=0;i<4;i++)a[i][i]=1;
   a[3][0]=float(int(random_word()%21)-10)/8;
   uint8_t flags=op==0?2:op==1?6:op==2?3:0;
   e.GfxSpMatrix(flags,(int32_t*)a);l.GfxSpMatrix(flags,(int32_t*)a);
  }else if(op==4){unsigned c=random_word()%15;e.GfxSpPopMatrix(c);l.GfxSpPopMatrix(c);}
  else if(op==5){e.SpReset();l.SpReset();}
  else if(op==6){unsigned offset=(random_word()%16)*4;unsigned bits=random_word()&0x000f000fu;
   e.GfxSpMovewordF3dex2(0,offset,bits);l.GfxSpMovewordF3dex2(0,offset,bits);
  }else {e.GfxSpVertex(1,0,nullptr);l.GfxSpVertex(1,0,nullptr);assert(memcmp(e.observed,l.observed,sizeof(e.observed))==0);}
  assert(e.mRsp->modelview_matrix_stack_size==l.mRsp->modelview_matrix_stack_size);
  assert(memcmp(e.mRsp->P_matrix,l.mRsp->P_matrix,sizeof(e.mRsp->P_matrix))==0);
  for(unsigned i=0;i<e.mRsp->modelview_matrix_stack_size;i++)
   assert(memcmp(e.mRsp->modelview_matrix_stack[i],l.mRsp->modelview_matrix_stack[i],sizeof(a))==0);
 }
 e.GfxSpVertex(1,0,nullptr);l.GfxSpVertex(1,0,nullptr);assert(memcmp(e.observed,l.observed,sizeof(e.observed))==0);
 assert(Lazy::Interpreter::products<Eager::Interpreter::products);
}
"""
        vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
        vs=subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
        with tempfile.TemporaryDirectory(prefix='matrix-product-proof-') as folder:
            out=Path(folder);(out/'proof.cpp').write_text(program)
            (out/'build.cmd').write_text('@echo off\ncall "'+vs+'/VC/Auxiliary/Build/vcvars64.bat" >nul\ncl /nologo /std:c++17 /EHsc /O2 /fp:precise /MT proof.cpp /Feproof.exe\nexit /b %errorlevel%\n')
            built=subprocess.run([os.environ['ComSpec'],'/d','/c',str(out/'build.cmd')],cwd=out,capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(out/'proof.exe')],capture_output=True,text=True,timeout=15)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
if __name__=='__main__':unittest.main()
