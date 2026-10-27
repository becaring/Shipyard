// Runs sources/xenos/shader_pack_program.cpp, the renderer's pack callback,
// against a pack built from the same stand-in builder and template.
//
// usage: program_contract <template> <pack> <id0:id1:filter:srgb>...
#include "xenos/shader_pack_program.h"
#include "xenos/shader_cache.h"
#include <cstdio>
#include <fstream>
#include <sstream>

std::string Shipyard_ShaderPackTemplate;

HRESULT D3DXCreateBuffer(DWORD, ID3DXBuffer** buffer) {
    *buffer = NULL;
    return E_OUTOFMEMORY;
}

int main(int argc, char** argv) {
    if (argc < 3)
        return 2;
    std::ifstream templ(argv[1], std::ios::binary);
    std::ostringstream bytes;
    bytes << templ.rdbuf();
    Shipyard_ShaderPackTemplate = bytes.str();
    std::printf("configured %d\n", Shipyard::Xenos::ConfigureShaderPack(argv[2]) ? 1 : 0);
    for (int i = 3; i < argc; ++i) {
        unsigned long long id0 = 0, id1 = 0;
        int filter = 0, srgb = 0;
        if (std::sscanf(argv[i], "%llx:%llx:%d:%d", &id0, &id1, &filter, &srgb) != 4)
            return 3;
        Fast::ShaderProgramKey key = {id0, id1, (Fast::FilteringMode)filter, srgb != 0};
        Fast::ShipyardXenos::ShaderProgramCode code = {NULL, 0, NULL, 0, 0};
        if (Shipyard::Xenos::PackedFast3DProgram(key, &code))
            std::printf("hit %s %u\n", argv[i], (unsigned)code.floats);
        else
            std::printf("miss %s\n", argv[i]);
    }
    return 0;
}
