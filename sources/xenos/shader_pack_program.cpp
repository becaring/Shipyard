// Prebuilt-pack program callback. It regenerates sources exactly as
// CreateShader does, so it must be compiled with core.cpp's capability set:
// the ABI it lowers with is the ABI the renderer would compile.
#include "configuration.h"
#if !SHIPYARD_XENOS_SHADER_PACK
#error Compile with xenos_capabilities.compiler_options(abi, shader_pack=True), as for core.cpp
#endif
#include "shader_pack_program.h"
#include "shader_cache.h"
#include "graphics/shader_lower.h"

namespace {
bool Source(uint64_t shaderId0, uint64_t shaderId1, bool threePoint, bool srgb, std::string& lowered,
            size_t& floats) {
    Fast::ShaderProgramKey key;
    key.shaderId0 = shaderId0;
    key.shaderId1 = shaderId1;
    // The builder distinguishes only three-point filtering.
    key.filteringMode = threePoint ? Fast::FILTER_THREE_POINT : Fast::FILTER_LINEAR;
    key.srgbMode = srgb;
    CCFeatures features;
    std::string hlsl, error;
    return Shipyard_BuildFast3DShader(key, features, floats, hlsl, error) &&
           Shipyard_LowerShader(hlsl, lowered, error, SHIPYARD_XENOS_SHADER_ABI);
}
} // namespace

namespace Shipyard {
namespace Xenos {
bool PackedFast3DProgram(const Fast::ShaderProgramKey& key, Fast::ShipyardXenos::ShaderProgramCode* code) {
    PackedShaderProgram packed;
    if (!code || !FindVerifiedPackedProgram(key.shaderId0, key.shaderId1,
                                            key.filteringMode == Fast::FILTER_THREE_POINT, key.srgbMode,
                                            SHIPYARD_XENOS_SHADER_ABI, Source, &packed))
        return false;
    code->vertex = packed.vertex;
    code->vertexBytes = packed.vertexBytes;
    code->pixel = packed.pixel;
    code->pixelBytes = packed.pixelBytes;
    code->floats = packed.floats;
    return true;
}
} // namespace Xenos
} // namespace Shipyard
