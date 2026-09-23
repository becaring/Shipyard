#include "shader_provider.h"
#include <exception>

namespace Fast {
std::string gfx_direct3d_common_build_shader(size_t&, const CCFeatures&, bool, bool, bool);
}

void Shipyard_DecodeFast3DFeatures(const Fast::ShaderProgramKey&, CCFeatures*);

bool Shipyard_BuildFast3DShader(const Fast::ShaderProgramKey& key, CCFeatures& features,
                               size_t& floats, std::string& hlsl, std::string& error) {
    try {
        Shipyard_DecodeFast3DFeatures(key, &features);
        hlsl = Fast::gfx_direct3d_common_build_shader(
            floats, features, false, key.filteringMode == Fast::FILTER_THREE_POINT, key.srgbMode);
        error.clear();
        return true;
    } catch (const std::exception& failure) {
        hlsl.clear();
        floats = 0;
        error = failure.what();
        return false;
    }
}
