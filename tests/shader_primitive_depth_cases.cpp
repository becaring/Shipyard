#include "graphics/shader_lower.h"
#include <cassert>
#include <string>
#include <iostream>

struct IDirect3DDevice9 {
    float depthScale = -1; unsigned uploads = 0;
    int SetVertexShaderConstantF(unsigned slot, const float* value, unsigned count) {
        assert(slot == 14 && count == 1);depthScale = value[0];++uploads;return 0;
    }
};
void Shipyard_Check(int result,const char*) { assert(result == 0); }
#include "xenos/depth_clip.h"

int main() {
    IDirect3DDevice9 device;
    Shipyard::Xenos::DepthClipState state;
    // Pinned orthographic HUD: z=0, near=10, far=60. D3D clip Z is -0.2.
    const float heartZ = -10.0f / (60.0f - 10.0f);
    state.SetDepth(false,false);state.Apply(&device);
    assert(heartZ < 0 && heartZ * device.depthScale == 0);
    state.Apply(&device);assert(device.uploads == 1);
    state.Invalidate();state.Apply(&device);assert(device.uploads == 2);
    for(unsigned mode=1;mode<4;++mode) {
        state.SetDepth((mode&1)!=0,(mode&2)!=0);state.Apply(&device);
        assert(device.depthScale == 1 && heartZ * device.depthScale == heartZ);
    }
    assert(device.uploads == 3);
    state.SetDepth(false,false);state.Apply(&device);assert(device.uploads == 4);

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
    const std::string vertex = "struct PSInput { float4 position : SV_POSITION; };\n"
        "PSInput VSMain(float4 position : POSITION) { PSInput result; result.position = position; return result; }\n";
    assert(Shipyard_LowerShader(frame + vertex, lowered, error));
    assert(lowered.find("float4 rasterDepthInfo : register(c14);") != std::string::npos);
    assert(lowered.find("result.position.z *= rasterDepthInfo.x;") != std::string::npos);
    assert(Shipyard_LowerShader(frame + vertex, lowered, error, 2));
    assert(lowered.find("// Shipyard Xenos lowering ABI 2\n") == 0);
    assert(lowered.find("rasterDepthInfo") == std::string::npos);
    assert(lowered.find("result.position = position;") != std::string::npos);
    const std::string abi2 = lowered;
    assert(!Shipyard_LowerShader(frame + depth + output, lowered, error, 2));
    assert(lowered == abi2);
    assert(!Shipyard_LowerShader(frame + vertex, lowered, error, 4));
    assert(lowered == abi2);
    // Unknown vertex forms must fail rather than silently omit depth behavior.
    std::string unknown = vertex;
    unknown.replace(unknown.find("result.position = position;"), 27, "result.position = position * 1.0;");
    assert(!Shipyard_LowerShader(frame + unknown, lowered, error));
    const std::string unchanged = lowered;
    assert(!Shipyard_LowerShader(frame + depth + depth + output, lowered, error));
    assert(lowered == unchanged);
    std::string wrong = depth;
    wrong.replace(wrong.find("float3 _pad"), 11, "float2 _pad");
    assert(!Shipyard_LowerShader(frame + wrong + output, lowered, error));
    assert(!Shipyard_LowerShader(frame + depth, lowered, error));
    std::cout << "PASS primitive depth optional contract and rejection\n";
}
