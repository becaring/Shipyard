#include "graphics/shader_lower.h"
#include <cassert>
#include <string>
#include <iostream>

int main() {
    const std::string frame = "cbuffer PerFrameCB : register(b0) { float noise_frame; float noise_scale; }\n";
    const std::string depth = "cbuffer PerPrimDepthCB : register(b2) {\n"
        "    float prim_depth;\n    float3 _pad; // pad to 16-byte cbuffer alignment\n}\n";
    const std::string output = "struct PSOutput { float4 color : SV_TARGET; float depth : SV_Depth; };\n";
    std::string lowered, error;
    assert(Shipyard_LowerShader(frame, lowered, error));
    assert(lowered.find("primDepthInfo") == std::string::npos);
    assert(Shipyard_LowerShader(frame + depth + output, lowered, error));
    assert(lowered.find("register(c3)") != std::string::npos);
    assert(lowered.find("float depth : DEPTH;") != std::string::npos);
    assert(lowered.find("#define prim_depth primDepthInfo.x") != std::string::npos);
    const std::string unchanged = lowered;
    assert(!Shipyard_LowerShader(frame + depth + depth + output, lowered, error));
    assert(lowered == unchanged);
    std::string wrong = depth;
    wrong.replace(wrong.find("float3 _pad"), 11, "float2 _pad");
    assert(!Shipyard_LowerShader(frame + wrong + output, lowered, error));
    assert(!Shipyard_LowerShader(frame + depth, lowered, error));
    std::cout << "PASS primitive depth optional contract and rejection\n";
}
