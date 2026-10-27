#pragma once
// Stand-in consumer for sources/graphics/shader_pack_sources.cpp and
// sources/xenos/shader_pack_program.cpp: the template with the key appended,
// and a key whose generation fails.
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <utility>

namespace Fast {
enum FilteringMode { FILTER_THREE_POINT, FILTER_LINEAR, FILTER_NONE };
struct ShaderProgramKey {
    uint64_t shaderId0;
    uint64_t shaderId1;
    FilteringMode filteringMode;
    bool srgbMode;
};
// Renderer types core.h names, for the pack program contract.
struct GfxClipParameters {
    bool z_is_from_0_to_1, invertY;
};
struct hash_pair_ff {
    size_t operator()(const std::pair<float, float>&) const { return 0; }
};
}
typedef void* ImTextureID;
struct CCFeatures {
    int unused;
};
extern std::string Shipyard_ShaderPackTemplate;

inline bool Shipyard_BuildFast3DShader(const Fast::ShaderProgramKey& key, CCFeatures&, size_t& floats,
                                       std::string& hlsl, std::string& error) {
    if (key.shaderId0 == 0xbad) {
        error = "unsupported fixture key";
        return false;
    }
    char tail[96];
    snprintf(tail, sizeof(tail), "// %016llx %016llx %d %d\n", (unsigned long long)key.shaderId0,
             (unsigned long long)key.shaderId1, key.filteringMode == Fast::FILTER_THREE_POINT ? 1 : 0,
             key.srgbMode ? 1 : 0);
    hlsl = Shipyard_ShaderPackTemplate + tail;
    floats = 4 + (size_t)(key.shaderId0 & 3);
    return true;
}
