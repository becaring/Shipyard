cbuffer PerFrameCB : register(b0) { float noise_frame; float noise_scale; }
struct PSInput { float4 position : SV_POSITION; };
PSInput VSMain(float4 position : POSITION) { PSInput result; result.position = position; return result; }
float4 PSMain(PSInput input) : SV_TARGET { return float4(noise_frame, noise_scale, 0, 1); }
