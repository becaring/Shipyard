#pragma once
#include "core.h"

namespace Shipyard {
namespace Xenos {
// ShaderCacheOps::program for Fast3D renderers: verified prebuilt-pack code
// for a program key (see FindVerifiedPackedProgram), regenerated with this
// build's Shipyard_BuildFast3DShader and SHIPYARD_XENOS_SHADER_ABI lowering.
bool PackedFast3DProgram(const Fast::ShaderProgramKey& key, Fast::ShipyardXenos::ShaderProgramCode* code);
} // namespace Xenos
} // namespace Shipyard
