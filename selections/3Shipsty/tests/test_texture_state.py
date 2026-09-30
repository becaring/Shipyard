"""Differential tests using a selected consumer's actual texture-state methods."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
from xbox360_platform.texture_state import metadata_reference, empty_mask_lookup


def method(source, name):
    start = source.index('void Interpreter::' + name + '(')
    return source[start:source.index('\n}', start) + 2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--modern-root', type=Path, required=True)
    args = parser.parse_args()
    source = (args.modern_root/'libultraship/src/fast/interpreter.cpp').read_text(encoding='utf-8-sig')
    header = (args.modern_root/'libultraship/include/fast/interpreter.h').read_text(encoding='utf-8-sig')
    names = ('GfxDpSetTextureImage', 'GfxDpLoadBlock', 'GfxDpLoadTile')
    original = '\n'.join(method(source, name) for name in names)
    transformed = empty_mask_lookup(metadata_reference(original))
    declaration_start = header.index('    void GfxDpSetTextureImage(')
    declaration = header[declaration_start:header.index(';', declaration_start)+1]
    assert 'const RawTexMetadata&' in metadata_reference(declaration)
    for function in (metadata_reference, empty_mask_lookup):
        try:
            function(transformed)
        except ValueError:
            pass
        else:
            raise AssertionError('Repeated projection must detect drift')
    start = header.index('struct RawTexMetadata {')
    metadata = header[start:header.index('\n};', start)+3]
    start = source.index('std::string Interpreter::GetBaseTexturePath(')
    base_path = source[start:source.index('\n}', start)+2]
    classes = []
    for name, methods in (('Original', original), ('Projected', transformed)):
        declarations = '\n'.join(method(methods, n).split('{',1)[0].replace('Interpreter::','')+';' for n in names)
        # Observe transient ownership at entry without replacing copy/assignment logic.
        methods = methods.replace('    mRdp->texture_to_load.addr =',
            '    observedRefs=rawTexMetdata.resource.use_count();\n    mRdp->texture_to_load.addr =', 1)
        classes.append('namespace '+name+''' {
struct Interpreter {
 RDP storage{}; RDP* mRdp=&storage;
 std::map<std::string,Mask> mMaskedTextures;
 long observedRefs=0;
 std::string GetBaseTexturePath(const std::string& path);
'''+declarations+'\n};\n'+base_path+'\n'+methods+'\n}\n')
    driver = r'''
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#define SUPPORT_CHECK(x) assert(x)
#define G_TX_LOADTILE 7
#define G_TEXTURE_IMAGE_FRAC 2
#define G_IM_SIZ_4b 0
#define G_IM_SIZ_8b 1
#define G_IM_SIZ_16b 2
#define G_IM_SIZ_32b 3
static int pathReads=0;
struct InitData { std::string Path; };
namespace Ship { struct IResource { inline static const std::string gAltAssetPrefix="alt/"; }; }
namespace Fast {
enum class TextureType { Error, RGBA };
struct Texture {
 std::shared_ptr<InitData> init=std::make_shared<InitData>();
 std::shared_ptr<InitData> GetInitData(){++pathReads;return init;}
}; }
''' + metadata + r'''
struct State {
 const uint8_t* addr=nullptr;
 uint32_t siz=0,width=0,tex_flags=0,orig_size_bytes=0,size_bytes=0,line_size_bytes=0,full_image_line_size_bytes=0;
 RawTexMetadata raw_tex_metadata{};
 bool masked=false,blended=false;
};
struct Tile { unsigned tmem_index=0; float uls=0,ult=0,lrs=0,lrt=0; };
struct RDP { State texture_to_load,loaded_texture[2];Tile texture_tile[8];bool textures_changed[2]={false,false}; };
struct Mask { const void* replacementData=nullptr; };
''' + '\n'.join(classes) + r'''
template<class T> std::string scenario(bool projected){
 T owner;std::ostringstream out;static uint8_t bytes[4096];
 auto resource=std::make_shared<Fast::Texture>();std::weak_ptr<Fast::Texture> weak=resource;
 RawTexMetadata metadata{};metadata.width=32;metadata.height=16;metadata.type=Fast::TextureType::RGBA;
 metadata.resource=resource;metadata.h_byte_scale=2;metadata.v_pixel_scale=1.5f;
 long before=resource.use_count();
 owner.GfxDpSetTextureImage(0,1,32,"unused",3,metadata,bytes);
 assert(owner.observedRefs==before+(projected?0:1));
 assert(resource.use_count()==before+1);
 for(unsigned slot=0;slot<2;++slot)for(unsigned size=1;size<4;++size){
  owner.storage.texture_tile[7].tmem_index=slot;
  owner.storage.texture_to_load.siz=size;
  for(int scenario=0;scenario<7;++scenario){
   owner.mMaskedTextures.clear();resource->init->Path="textures/test";
   owner.storage.texture_to_load.raw_tex_metadata.resource=resource;
   if(scenario==1)owner.mMaskedTextures["unrelated"]={};
   if(scenario==2)owner.mMaskedTextures["textures/test"]={};
   if(scenario==3){resource->init->Path="alt/textures/test";owner.mMaskedTextures["textures/test"]={bytes};}
   if(scenario==4){owner.storage.texture_to_load.raw_tex_metadata.resource.reset();owner.mMaskedTextures[""]={bytes};}
   if(scenario==5)resource->init->Path="alt/textures/test";
   // Scenario 6 remains empty after prior dynamic mask additions/removals.
   for(int tile=0;tile<2;++tile){
    auto& state=owner.storage.loaded_texture[slot];state.masked=state.blended=true;
    pathReads=0;
    if(tile)owner.GfxDpLoadTile(7,4,4,28,20);else owner.GfxDpLoadBlock(7,0,0,31,0);
    assert(pathReads==((projected && owner.mMaskedTextures.empty())||scenario==4?0:1));
    out<<slot<<size<<scenario<<tile<<state.masked<<state.blended<<':'
       <<state.orig_size_bytes<<','<<state.size_bytes<<','<<state.line_size_bytes<<','
       <<state.full_image_line_size_bytes<<','<<(state.addr-bytes)<<','<<state.tex_flags<<','
       <<state.raw_tex_metadata.width<<','<<state.raw_tex_metadata.height<<','
       <<state.raw_tex_metadata.h_byte_scale<<','<<state.raw_tex_metadata.v_pixel_scale<<','
       <<bool(state.raw_tex_metadata.resource)<<','<<owner.storage.textures_changed[slot]<<';';
   }
  }
 }
 metadata.resource.reset();resource.reset();assert(!weak.expired());
 RawTexMetadata empty{};owner.GfxDpSetTextureImage(0,1,0,nullptr,0,empty,bytes);
 owner.storage.loaded_texture[0].raw_tex_metadata.resource.reset();
 owner.storage.loaded_texture[1].raw_tex_metadata.resource.reset();assert(weak.expired());
 return out.str();
}
int main(){assert(scenario<Original::Interpreter>(false)==scenario<Projected::Interpreter>(true));
 std::cout<<"Texture metadata ownership and 84 masked/unmasked load transitions match\n";}
'''
    vswhere = Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
    installation = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires',
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
    with tempfile.TemporaryDirectory(prefix='shipyard-texture-state-') as folder:
        output=Path(folder);(output/'contract.cpp').write_text(driver)
        batch=output/'compile.cmd'
        batch.write_text('@echo off\ncall "'+installation+'/VC/Auxiliary/Build/vcvars64.bat" >nul\n'
            'if errorlevel 1 exit /b 1\ncl /nologo /std:c++20 /EHsc /MT contract.cpp /Fecontract.exe\nexit /b %errorlevel%\n')
        built=subprocess.run([os.environ['ComSpec'],'/d','/c',str(batch)],cwd=output,capture_output=True,text=True)
        assert built.returncode==0,built.stdout+built.stderr
        result=subprocess.run([str(output/'contract.exe')],capture_output=True,text=True)
        assert result.returncode==0,result.stdout+result.stderr
        print(result.stdout.strip())


if __name__=='__main__':main()
