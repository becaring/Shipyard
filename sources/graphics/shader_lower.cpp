#include "shader_lower.h"
#include <cstdio>
#include <cstdlib>
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

// Fast3D combiner inputs are 8-bit colors over 255 and its textures are unorm,
// so both lie in [0, 1]. A combiner WRAP whose operand provably stays inside
// [low, high) is the identity; most are, and they are a fifth of the ALU.
struct Range {
    double low, high;
};
struct Value {
    Range rgb, a; // A scalar has rgb == a.
};
const Range Unit = {0.0, 1.0};

Range Hull(const Range& x, const Range& y) {
    const Range r = {x.low < y.low ? x.low : y.low, x.high > y.high ? x.high : y.high};
    return r;
}

bool Apply(char op, const Range& x, const Range& y, Range& out) {
    if (op == '/') {
        if (y.low <= 0.0)
            return false;
        const Range inverse = {1.0 / y.high, 1.0 / y.low};
        return Apply('*', x, inverse, out);
    }
    if (op == '+') {
        out.low = x.low + y.low;
        out.high = x.high + y.high;
    } else if (op == '-') {
        out.low = x.low - y.high;
        out.high = x.high - y.low;
    } else {
        const double p[] = {x.low * y.low, x.low * y.high, x.high * y.low, x.high * y.high};
        out.low = out.high = p[0];
        for (int i = 1; i < 4; ++i) {
            out.low = p[i] < out.low ? p[i] : out.low;
            out.high = p[i] > out.high ? p[i] : out.high;
        }
    }
    return true;
}

bool Apply(char op, const Value& x, const Value& y, Value& out) {
    return Apply(op, x.rgb, y.rgb, out.rgb) && Apply(op, x.a, y.a, out.a);
}

// Reads [-]digits[.digits] without the CRT, so the Xbox and the offline shader
// pack generator lower byte for byte alike (their sscanf/strtod differ).
bool ReadNumber(const std::string& text, size_t& at, double& out) {
    const bool negative = at < text.size() && text[at] == '-';
    size_t i = at + (negative ? 1 : 0), digits = 0;
    double value = 0.0, scale = 1.0;
    for (; i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])); ++i, ++digits)
        value = value * 10.0 + (text[i] - '0');
    if (i < text.size() && text[i] == '.')
        for (++i; i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])); ++i, ++digits)
            value += (text[i] - '0') * (scale *= 0.1);
    if (!digits)
        return false;
    out = negative ? -value : value;
    at = i;
    return true;
}

// Bounds one combiner formula as Fast3D's append_formula spells it.
class RangeParser {
public:
    RangeParser(const std::string& text, const Value* texel) : text_(text), at_(0), texel_(texel) {}
    bool Parse(Value& out) {
        if (!Sum(out))
            return false;
        Skip();
        return at_ == text_.size();
    }

private:
    void Skip() {
        while (at_ < text_.size() && text_[at_] == ' ')
            ++at_;
    }
    bool Take(char c) {
        Skip();
        if (at_ < text_.size() && text_[at_] == c) {
            ++at_;
            return true;
        }
        return false;
    }
    bool Sum(Value& out) {
        if (!Product(out))
            return false;
        for (;;) {
            const char op = Take('+') ? '+' : Take('-') ? '-' : 0;
            Value right;
            if (!op)
                return true;
            if (!Product(right) || !Apply(op, out, right, out))
                return false;
        }
    }
    bool Product(Value& out) {
        if (!Unary(out))
            return false;
        for (;;) {
            const char op = Take('*') ? '*' : Take('/') ? '/' : 0;
            Value right;
            if (!op)
                return true;
            if (!Unary(right) || !Apply(op, out, right, out))
                return false;
        }
    }
    bool Unary(Value& out) {
        if (!Take('-'))
            return Atom(out);
        const Value zero = {{0.0, 0.0}, {0.0, 0.0}};
        Value operand;
        return Unary(operand) && Apply('-', zero, operand, out);
    }
    bool Arguments(Value* out, unsigned capacity, unsigned& count) {
        count = 0;
        if (!Take('('))
            return false;
        do {
            if (count == capacity || !Sum(out[count++]))
                return false;
        } while (Take(','));
        return Take(')');
    }
    bool Atom(Value& out) {
        if (Take('(')) {
            return Sum(out) && Take(')');
        }
        Skip();
        const size_t begin = at_;
        double number = 0.0;
        if (at_ < text_.size() && text_[at_] != '-' && ReadNumber(text_, at_, number)) {
            const Range r = {number, number};
            out.rgb = out.a = r;
            return true;
        }
        while (at_ < text_.size() &&
               (std::isalnum(static_cast<unsigned char>(text_[at_])) || text_[at_] == '_' || text_[at_] == '.'))
            ++at_;
        const std::string name = text_.substr(begin, at_ - begin);
        if (name == "random") {
            // frac(sin(...) * k): skip the seed expression.
            unsigned depth = 0;
            do {
                if (at_ == text_.size())
                    return false;
                depth += text_[at_] == '(';
                depth -= text_[at_] == ')';
                ++at_;
            } while (depth);
            out.rgb = out.a = Unit;
            return true;
        }
        Value args[4];
        unsigned count = 0;
        if (name == "lerp") {
            if (!Arguments(args, 3, count) || count != 3)
                return false;
            Range* channel[2] = {&out.rgb, &out.a};
            const Range* from[2] = {&args[0].rgb, &args[0].a};
            const Range* to[2] = {&args[1].rgb, &args[1].a};
            const Range* weight[2] = {&args[2].rgb, &args[2].a};
            for (int i = 0; i < 2; ++i) {
                if (weight[i]->low >= 0.0 && weight[i]->high <= 1.0) {
                    *channel[i] = Hull(*from[i], *to[i]);
                    continue;
                }
                Range delta, scaled;
                if (!Apply('-', *to[i], *from[i], delta) || !Apply('*', *weight[i], delta, scaled) ||
                    !Apply('+', *from[i], scaled, *channel[i]))
                    return false;
            }
            return true;
        }
        if (name == "float3" || name == "float4") {
            if (!Arguments(args, 4, count))
                return false;
            if (count == 1) {
                out = args[0];
            } else if (name == "float4" && count == 2) {
                out.rgb = args[0].rgb;
                out.a = args[1].a;
            } else if (count == (name == "float4" ? 4u : 3u)) {
                out.rgb = Hull(Hull(args[0].rgb, args[1].rgb), args[2].rgb);
                out.a = name == "float4" ? args[3].a : out.rgb;
            } else {
                return false;
            }
            return true;
        }
        const size_t dot = name.find('.');
        const std::string base = name.substr(0, dot), swizzle = dot == std::string::npos ? "" : name.substr(dot);
        if ((base.size() == 7 && base.compare(0, 6, "texVal") == 0 && std::isdigit(static_cast<unsigned char>(base[6]))) ||
            name.compare(0, 11, "input.input") == 0) {
            out.rgb = out.a = Unit;
            return true;
        }
        if (base != "texel" || !texel_)
            return false;
        if (swizzle == ".a") {
            out.rgb = out.a = texel_->a;
        } else if (swizzle == ".rgb") {
            out.rgb = out.a = texel_->rgb;
        } else if (swizzle.empty()) {
            out = *texel_;
        } else {
            return false;
        }
        return true;
    }

    const std::string& text_;
    size_t at_;
    const Value* texel_;
};

bool Inside(const Range& r, double low, double high) {
    return low < r.low && r.high < high;
}

// Drops identity WRAPs between the texel declaration and the final clamp.
// Any statement it does not recognise leaves the shader unchanged.
void FoldCombinerWraps(std::string& text) {
    const bool vector = text.find("float4 texel;") != std::string::npos;
    const size_t start = text.find(vector ? "float4 texel;" : "float3 texel;");
    const size_t end = text.find("texel = clamp(texel, 0.0, 1.0);");
    if (start == std::string::npos || end == std::string::npos || end < start)
        return;
    const std::string body = text.substr(start, end - start);
    const std::string alphaTest = "if (texel.a > ", alphaPass = ") texel.a = 1.0; else clip(-1.0);";
    std::string out;
    Value texel;
    bool known = false;
    for (size_t begin = 0, line = 0; begin <= body.size(); ++line) {
        size_t next = body.find('\n', begin);
        if (next == std::string::npos)
            next = body.size();
        const std::string raw = body.substr(begin, next - begin);
        begin = next + 1;
        size_t first = raw.find_first_not_of(' '), last = raw.find_last_not_of(' ');
        const std::string s = first == std::string::npos ? "" : raw.substr(first, last - first + 1);
        bool keep = true;
        if (line == 0 || s.empty()) {
        } else if (s.find("WRAP") != std::string::npos) {
            const char* groups[] = {"", ".rgb", ".a"};
            bool matched = false;
            for (int g = 0; g < 3 && !matched; ++g) {
                const std::string prefix =
                    std::string("texel") + groups[g] + " = WRAP(texel" + groups[g] + ", ";
                double low = 0.0, high = 0.0;
                size_t at = prefix.size();
                if (s.compare(0, prefix.size(), prefix) != 0 || !ReadNumber(s, at, low) ||
                    s.compare(at, 2, ", ") != 0 || !ReadNumber(s, at += 2, high) || s.compare(at, std::string::npos, ");") != 0)
                    continue;
                matched = true;
                const bool rgb = g != 2 && !(known && Inside(texel.rgb, low, high));
                const bool alpha = g != 1 && !(known && Inside(texel.a, low, high));
                const Range wrapped = {low, high};
                keep = rgb || alpha;
                if (known && rgb)
                    texel.rgb = wrapped;
                if (known && alpha)
                    texel.a = vector ? wrapped : texel.rgb;
            }
            if (!matched)
                return;
        } else if (s.compare(0, 8, "texel = ") == 0 && s[s.size() - 1] == ';') {
            known = RangeParser(s.substr(8, s.size() - 9), known ? &texel : NULL).Parse(texel);
            if (known && !vector)
                texel.a = texel.rgb;
        } else if (s.compare(0, alphaTest.size(), alphaTest) == 0 && s.size() > alphaPass.size() &&
                   s.compare(s.size() - alphaPass.size(), alphaPass.size(), alphaPass) == 0) {
            const Range one = {1.0, 1.0};
            texel.a = one;
        } else {
            return;
        }
        if (keep)
            out += raw + (begin <= body.size() ? "\n" : "");
    }
    text.replace(start, end - start, out);
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

bool Shipyard_LowerShader(const std::string& hlsl, std::string& lowered, std::string& error, unsigned abi) {
    if (abi != 2 && abi != 3) {
        error = "Unsupported Xenos shader ABI";
        return false;
    }
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
    if (abi >= 3 && primitive != std::string::npos) {
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
    if (abi >= 3 && text.find("VSMain(") != std::string::npos) {
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
    // Three-point weights the lower triangle's texels (1-x-y, x, y) and the
    // upper's (x+y-1, 1-y, 1-x). Fetching all four from one point with fetch
    // offsets moves the coordinate math off the ALU; the shader is ALU-bound.
    Block(text, "float4 tex2D3PointFilter(",
          "float4 tex2D3PointFilter(in Texture2D tex, in SamplerState tSampler, in float2 texCoord, "
          "in float2 texSize) {\n"
          "    float2 f = frac(texCoord * texSize - float2(0.5, 0.5));\n"
          "    float2 base = texCoord - f / texSize;\n"
          "    float s = f.x + f.y - 1.0;\n"
          "    float2 w = max(float2(s, -s), 0.0);\n"
          "    float2 n = f - w.x;\n"
          "    float4 t00, t10, t01, t11;\n"
          "    asm {\n"
          "        tfetch2D t00, base, tex, OffsetX=0.0, OffsetY=0.0\n"
          "        tfetch2D t10, base, tex, OffsetX=1.0, OffsetY=0.0\n"
          "        tfetch2D t01, base, tex, OffsetX=0.0, OffsetY=1.0\n"
          "        tfetch2D t11, base, tex, OffsetX=1.0, OffsetY=1.0\n"
          "    };\n"
          "    return t00 * w.y + t10 * n.x + t01 * n.y + t11 * w.x;\n"
          "}");
    LowerTextureSlots(text);
    LowerTextureInfo(text);
    LowerSemantics(text);
    FoldCombinerWraps(text);
    if (!RejectUnsupported(text, error)) {
        return false;
    }

    lowered = std::string(abi == 2 ? "// Shipyard Xenos lowering ABI 2\n" : "// Shipyard Xenos lowering ABI 3\n") +
              "float4 framebufferTile : register(c4);\n"
              "float4 bindingInfo[6] : register(c8);\n" +
              text;
    error.clear();
    return true;
}
