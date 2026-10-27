// Reads a pack written by xbox360_platform.shader_pack and reports verified
// lookups. The stand-in build regenerates "source <id0> <id1> <tp> <srgb>"
// with id0's low byte plus 4 floats; one id0 may be made to differ.
//
// usage: contract <pack> <abi> <match|source|floats> <id0 hex> <id0:id1:flags>...
#include "xenos/shader_cache.h"
#include "xenos/shader_pack.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

HRESULT D3DXCreateBuffer(DWORD, ID3DXBuffer** buffer) {
    *buffer = NULL;
    return E_OUTOFMEMORY;
}

static unsigned long long differ = ~0ull;
static bool floatsDiffer;
static unsigned generated;

static bool Source(uint64_t id0, uint64_t id1, bool threePoint, bool srgb, std::string& lowered, size_t& floats) {
    char text[96];
    std::snprintf(text, sizeof(text), "source %016llx %016llx %d %d", (unsigned long long)id0,
                  (unsigned long long)id1, threePoint ? 1 : 0, srgb ? 1 : 0);
    lowered = text;
    floats = (size_t)(id0 & 0xff) + 4;
    if (id0 == differ) {
        if (floatsDiffer)
            ++floats;
        else
            lowered += " changed";
    }
    ++generated;
    return true;
}

int main(int argc, char** argv) {
    using namespace Shipyard::Xenos;
    if (argc < 5)
        return 2;
    const unsigned abi = (unsigned)std::strtoul(argv[2], NULL, 10);
    floatsDiffer = !std::strcmp(argv[3], "floats");
    if (std::strcmp(argv[3], "match"))
        differ = std::strtoull(argv[4], NULL, 16);
    std::printf("configured %d\n", ConfigureShaderPack(argv[1]) ? 1 : 0);
    for (int i = 5; i < argc; ++i) {
        unsigned long long id0 = 0, id1 = 0;
        unsigned flags = 0;
        if (std::sscanf(argv[i], "%llx:%llx:%u", &id0, &id1, &flags) != 3)
            return 3;
        PackedShaderProgram program;
        std::memset(&program, 0, sizeof(program));
        const bool hit = FindVerifiedPackedProgram(id0, id1, (flags & 1) != 0, (flags & 2) != 0, abi, Source,
                                                   &program);
        if (hit)
            std::printf("hit %s %u %u %u %.4s\n", argv[i], (unsigned)program.vertexBytes, (unsigned)program.pixelBytes,
                        (unsigned)program.floats, (const char*)program.vertex);
        else
            std::printf("miss %s\n", argv[i]);
        // A repeated lookup must give the same answer without regenerating.
        const unsigned before = generated;
        if (FindVerifiedPackedProgram(id0, id1, (flags & 1) != 0, (flags & 2) != 0, abi, Source, &program) != hit ||
            generated != before)
            return 4;
    }
    ShaderCacheCounts counts;
    std::memset(&counts, 0, sizeof(counts));
    GetShaderCacheCounts(&counts);
    std::printf("counts %u %u\n", counts.packHits, counts.packRejected);
    return 0;
}
