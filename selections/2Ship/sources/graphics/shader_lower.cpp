#include "shader_lower.h"
#include <cstdio>
#include <cctype>

namespace {

void Replace(std::string& text, const std::string& from, const std::string& to) {
    size_t position = 0;
    while ((position = text.find(from, position)) != std::string::npos) {
        text.replace(position, from.size(), to);
        position += to.size();
    }
}

bool Block(std::string& text, const char* name, const char* replacement) {
    const size_t begin = text.find(name);
    if (begin == std::string::npos) {
        return false;
    }

    size_t cursor = text.find('{', begin);
    if (cursor == std::string::npos) {
        return false;
    }

    unsigned depth = 1;
    while (++cursor < text.size() && depth) {
        if (text[cursor] == '{') {
            ++depth;
        } else if (text[cursor] == '}') {
            --depth;
        }
    }
    if (depth) {
        return false;
    }

    text.replace(begin, cursor - begin, replacement);
    return true;
}

bool PackClampVaryings(std::string& text, std::string& error) {
    // The largest current Fast3D layout exceeds Xenos's pixel input-variable
    // limit if four scalar clamps each consume a varying. Pack only the
    // VS->PS interface. VS input semantics and CPU vertex layout stay intact.
    const size_t begin = text.find("struct PSInput {");
    if (begin == std::string::npos) {
        return true;
    }

    const size_t end = text.find("};", begin);
    if (end == std::string::npos) {
        error = "Malformed Fast3D PSInput";
        return false;
    }

    std::string body = text.substr(begin, end - begin);
    bool any = false;
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            char declaration[100];
            std::sprintf(declaration, "float texClamp%s%d : TEXCLAMP%s%d;", j ? "T" : "S", i,
                         j ? "T" : "S", i);
            if (body.find(declaration) != std::string::npos) {
                Replace(body, declaration, "");
                any = true;
            }
        }
    }
    if (!any) {
        return true;
    }
    if (body.find("TEXCOORD11") != std::string::npos ||
        text.find("PSInput result;") == std::string::npos) {
        error = "Unsupported Fast3D clamp-varying layout";
        return false;
    }

    body += "float4 texClampPacked : TEXCOORD11;\n";
    text.replace(begin, end - begin, body);
    Replace(text, "PSInput result;",
            "PSInput result;\nresult.texClampPacked = float4(0, 0, 0, 0);");

    const char* component[] = {"x", "y", "z", "w"};
    const char* object[] = {"result", "input"};
    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            for (int k = 0; k < 2; ++k) {
                char from[64], to[64];
                std::sprintf(from, "%s.texClamp%s%d", object[k], j ? "T" : "S", i);
                std::sprintf(to, "%s.texClampPacked.%s", object[k], component[2 * i + j]);
                Replace(text, from, to);
            }
        }
    }
    return true;
}

void LowerTextureSlots(std::string& text) {
    for (int slot = 0; slot < 6; ++slot) {
        char texture[40], declaration[120], replacement[120];
        if (slot < 2) {
            std::sprintf(texture, "g_texture%d", slot);
        } else {
            std::sprintf(texture, slot < 4 ? "g_textureMask%d" : "g_textureBlend%d", slot % 2);
        }

        std::sprintf(declaration, "Texture2D %s : register(t%d);", texture, slot);
        std::sprintf(replacement, "sampler2D %s : register(s%d);", texture, slot);
        Replace(text, declaration, replacement);

        char sample[100], fetch[100];
        std::sprintf(sample, "%s.Sample(g_sampler%d, ", texture, slot % 2);
        std::sprintf(fetch, "tex2D(%s, ", texture);
        Replace(text, sample, fetch);
        std::sprintf(sample, "%s, g_sampler%d, ", texture, slot % 2);
        std::sprintf(fetch, "%s, ", texture);
        Replace(text, sample, fetch);
    }
}

void LowerTextureInfo(std::string& text) {
    for (int i = 0; i < 2; ++i) {
        char from[160], to[160];
        std::sprintf(from, "SamplerState g_sampler%d : register(s%d);", i, i);
        Replace(text, from, "");

        const char* member[] = {"width", "height", "linear_filtering"};
        const char* component[] = {"x", "y", "z"};
        for (int j = 0; j < 3; ++j) {
            std::sprintf(from, "textures[%d].%s", i, member[j]);
            std::sprintf(to, "texInfo[%d].%s", i, component[j]);
            Replace(text, from, to);
        }

        std::sprintf(from, "int2 texSize%d;", i);
        std::sprintf(to, "float2 texSize%d = texInfo[%d].xy;", i, i);
        Replace(text, from, to);
        std::sprintf(from, "g_texture%d.GetDimensions(texSize%d.x, texSize%d.y);", i, i, i);
        Replace(text, from, "");

        std::sprintf(from, "float2 maskSize%d;", i);
        std::sprintf(to, "float2 maskSize%d = bindingInfo[%d].xy;", i, i + 2);
        Replace(text, from, to);
        std::sprintf(from, "g_textureMask%d.GetDimensions(maskSize%d.x, maskSize%d.y);", i, i, i);
        Replace(text, from, "");

        for (int j = 0; j < 2; ++j) {
            std::sprintf(from, ": TEXCLAMP%s%d", j ? "T" : "S", i);
            std::sprintf(to, ": TEXCOORD%d", 11 + i * 2 + j);
            Replace(text, from, to);
        }
    }
}

void LowerSemantics(std::string& text) {
    Replace(text, "tex.Sample(tSampler, ", "tex2D(tex, ");
    Replace(text, "in Texture2D tex, in SamplerState tSampler,", "in sampler2D tex,");
    Replace(text, ": FOG", ": TEXCOORD9");
    Replace(text, ": GRAYSCALE", ": TEXCOORD10");
    for (int i = 0; i < 7; ++i) {
        char from[32], to[32];
        std::sprintf(from, ": INPUT%d", i);
        std::sprintf(to, ": TEXCOORD%d", i + 2);
        Replace(text, from, to);
    }
    Replace(text, "float4 screenSpace : SV_Position", "float2 screenSpace : VPOS");
    Replace(text, "SV_POSITION", "POSITION");
    Replace(text, "SV_TARGET", "COLOR0");
    Replace(text, "discard;", "clip(-1.0);");
    Replace(text, "screenSpace.xy * noise_scale",
            "(screenSpace.xy + framebufferTile.xy) * noise_scale");
}

bool RejectUnsupported(const std::string& text, std::string& error) {
    const char* unsupported[] = {"Texture2D", "SamplerState", ".Sample(",   ".GetDimensions(",
                                 "cbuffer ",  "register(t",   "register(b", "SV_"};
    for (unsigned i = 0; i < sizeof(unsupported) / sizeof(*unsupported); ++i) {
        if (text.find(unsupported[i]) != std::string::npos) {
            error = std::string("Unsupported generated HLSL construct: ") + unsupported[i];
            return false;
        }
    }
    return true;
}

} // namespace

bool Shipyard_LowerShader(const std::string& hlsl, std::string& lowered, std::string& error) {
    std::string text = hlsl;
    Replace(text, "\r\n", "\n");
    if (text.find("@prism") != std::string::npos) {
        error = "Shader provider supplied unexpanded Prism template";
        return false;
    }
    if (!PackClampVaryings(text, error)) {
        return false;
    }

    if (!Block(text, "cbuffer PerFrameCB : register(b0)",
               "float4 frameInfo : register(c0);\n#define noise_frame frameInfo.x\n"
               "#define noise_scale frameInfo.y\n")) {
        error = "Pinned Fast3D PerFrameCB contract is missing";
        return false;
    }
    if (text.find("cbuffer PerDrawCB") != std::string::npos &&
        !Block(text, "cbuffer PerDrawCB : register(b1)", "float4 texInfo[2] : register(c1);\n")) {
        error = "Malformed Fast3D PerDrawCB";
        return false;
    }
    const size_t primitive = text.find("cbuffer PerPrimDepthCB");
    if (primitive != std::string::npos) {
        const size_t end = text.find('}', primitive);
        std::string shape;
        if (end != std::string::npos) {
            for (size_t i = primitive; i <= end; ++i)
                if (!std::isspace(static_cast<unsigned char>(text[i]))) shape += text[i];
        }
        if ((shape != "cbufferPerPrimDepthCB:register(b2){floatprim_depth;float3_pad;}" &&
             shape != "cbufferPerPrimDepthCB:register(b2){floatprim_depth;float3_pad;//padto16-bytecbufferalignment}") ||
            text.find("cbuffer PerPrimDepthCB", primitive + 1) != std::string::npos ||
            text.find("float depth : SV_Depth;") == std::string::npos) {
            error = "Unsupported Fast3D primitive-depth contract";
            return false;
        }
        text.replace(primitive, end - primitive + 1,
                     "float4 primDepthInfo : register(c3);\n#define prim_depth primDepthInfo.x\n");
        Replace(text, "float depth : SV_Depth;", "float depth : DEPTH;");
    }

    // Clamp shaders need dimensions even when three-point filtering is off.
    if (text.find("float4 texInfo[2]") == std::string::npos) {
        text = "float4 texInfo[2] : register(c1);\n" + text;
    }
    if (text.find("VSMain(") != std::string::npos) {
        const std::string assignment = "result.position = position;";
        const size_t position = text.find(assignment);
        if (position == std::string::npos ||
            text.find(assignment, position + assignment.size()) != std::string::npos) {
            error = "Unsupported Fast3D vertex position contract";
            return false;
        }
        text.replace(position, assignment.size(), assignment +
                     "\n    result.position.z *= rasterDepthInfo.x;");
        text = "float4 rasterDepthInfo : register(c14);\n" + text;
    }
    LowerTextureSlots(text);
    LowerTextureInfo(text);
    LowerSemantics(text);
    if (!RejectUnsupported(text, error)) {
        return false;
    }

    lowered = "// Shipyard Xenos lowering ABI 3\n"
              "float4 framebufferTile : register(c4);\n"
              "float4 bindingInfo[6] : register(c8);\n" +
              text;
    error.clear();
    return true;
}
