#include "shipyard_imgui_xenos.h"
#include <d3dx9.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <vector>

namespace {
IDirect3DVertexShader9* vertex_shader;
IDirect3DPixelShader9* pixel_shader;
IDirect3DVertexDeclaration9* vertex_declaration;
IDirect3DStateBlock9* saved_state;
ImTextureID font_texture;
struct Vertex {
    float x, y, u, v, r, g, b, a;
};
std::vector<Vertex> vertices;
inline float UnitU8(uint8_t value) {
    return static_cast<float>(value) / 255.0f;
}
ID3DXBuffer* Compile(const char* source, const char* entry, const char* profile) {
    ID3DXBuffer* code = NULL;
    ID3DXBuffer* errors = NULL;
    HRESULT result = D3DXCompileShader(source, (UINT)strlen(source), NULL, NULL, entry, profile, 0,
                                       &code, &errors, NULL);
    if (errors) {
        Shipyard_ImGuiLogShader((const char*)errors->GetBufferPointer());
        errors->Release();
    }
    Shipyard_ImGuiCheck(result, "ImGui shader compile");
    return code;
}
void Setup(const ImDrawData* data) {
    IDirect3DDevice9* const device = Shipyard_ImGuiDevice();
    Shipyard_ImGuiInvalidateNativeState();
    D3DVIEWPORT9 viewport = {0,
                             0,
                             (DWORD)(data->DisplaySize.x * data->FramebufferScale.x),
                             (DWORD)(data->DisplaySize.y * data->FramebufferScale.y),
                             0,
                             1};
    device->SetViewport(&viewport);
    device->SetRenderState(D3DRS_VIEWPORTENABLE, TRUE);
    device->SetRenderState(D3DRS_HALFPIXELOFFSET, TRUE);
    device->SetVertexDeclaration(vertex_declaration);
    device->SetVertexShader(vertex_shader);
    device->SetPixelShader(pixel_shader);
    float transform[4] = {2.0f / data->DisplaySize.x, -2.0f / data->DisplaySize.y,
                          -1.0f - 2.0f * data->DisplayPos.x / data->DisplaySize.x,
                          1.0f + 2.0f * data->DisplayPos.y / data->DisplaySize.y};
    // As with framebuffer restores, Xenos applies the half-pixel adjustment.
    // The desktop D3D9 translation would offset glyphs and the game image twice.
    device->SetVertexShaderConstantF(0, transform, 1);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
    device->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_BLENDOPALPHA, D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 15);
    // Xenos gamma conversion is selected by the surface/texture format. These
    // UI resources use linear A8R8G8B8, so there is no desktop sRGB state here.
    device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    Shipyard_ImGuiInvalidateNativeState();
}
} // namespace
ImTextureID Shipyard_ImGuiUploadRGBA(const unsigned char* pixels, int width, int height) {
    if (!pixels || width <= 0 || height <= 0 || width > 4096 || height > 4096)
        Shipyard_ImGuiFatal("ImGui texture dimensions", E_INVALIDARG);
    IDirect3DTexture9* texture = NULL;
    Shipyard_ImGuiCheck(Shipyard_ImGuiDevice()->CreateTexture(width, height, 1, 0,
                                                              D3DFMT_LIN_A8R8G8B8, D3DPOOL_DEFAULT,
                                                              &texture, NULL),
                        "ImGui texture");
    D3DLOCKED_RECT lock;
    Shipyard_ImGuiCheck(texture->LockRect(0, &lock, NULL, 0), "ImGui texture upload");
    for (int y = 0; y < height; ++y) {
        DWORD* row = (DWORD*)((BYTE*)lock.pBits + y * lock.Pitch);
        for (int x = 0; x < width; ++x) {
            const unsigned char* p = pixels + (y * width + x) * 4;
            row[x] = ((DWORD)p[3] << 24) | ((DWORD)p[0] << 16) | ((DWORD)p[1] << 8) | p[2];
        }
    }
    texture->UnlockRect(0);
    return (ImTextureID)texture;
}
void Shipyard_ImGuiDestroyTexture(ImTextureID texture) {
    if (texture)
        ((IDirect3DTexture9*)texture)->Release();
}
static void BuildFontTexture() {
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels;
    int width;
    int height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    // ImGui stores ImU32 colors in native byte order; the upload accepts RGBA.
    std::vector<unsigned char> rgba((size_t)width * height * 4);
    const ImU32* packed = (const ImU32*)pixels;
    for (size_t i = 0; i < (size_t)width * height; ++i) {
        rgba[i * 4 + 0] = (unsigned char)(packed[i] >> IM_COL32_R_SHIFT);
        rgba[i * 4 + 1] = (unsigned char)(packed[i] >> IM_COL32_G_SHIFT);
        rgba[i * 4 + 2] = (unsigned char)(packed[i] >> IM_COL32_B_SHIFT);
        rgba[i * 4 + 3] = (unsigned char)(packed[i] >> IM_COL32_A_SHIFT);
    }
    ImTextureID replacement = Shipyard_ImGuiUploadRGBA(&rgba[0], width, height);
    io.Fonts->SetTexID(replacement);
    Shipyard_ImGuiDestroyTexture(font_texture);
    font_texture = replacement;
}
void Shipyard_ImGuiRendererInit() {
    IDirect3DDevice9* const device = Shipyard_ImGuiDevice();
    Shipyard_ImGuiInvalidateNativeState();
    static const char* source =
        "float4 transform : register(c0);\n"
        "struct VertexIn {float2 position:POSITION;float2 uv:TEXCOORD0;float4 color:COLOR0;};\n"
        "struct VertexOut {float4 position:POSITION;float2 uv:TEXCOORD0;float4 color:COLOR0;};\n"
        "VertexOut VSMain(VertexIn v){VertexOut "
        "o;o.position=float4(v.position*transform.xy+transform.zw,0,1);o.uv=v.uv;o.color=v.color;"
        "return o;}\n"
        "sampler2D image:register(s0);\n"
        "float4 PSMain(VertexOut v):COLOR0{return tex2D(image,v.uv)*v.color;}\n";
    ID3DXBuffer* code = Compile(source, "VSMain", "vs_3_0");
    Shipyard_ImGuiCheck(
        device->CreateVertexShader((DWORD*)code->GetBufferPointer(), &vertex_shader),
        "ImGui vertex shader");
    code->Release();
    code = Compile(source, "PSMain", "ps_3_0");
    Shipyard_ImGuiCheck(device->CreatePixelShader((DWORD*)code->GetBufferPointer(), &pixel_shader),
                        "ImGui pixel shader");
    code->Release();
    D3DVERTEXELEMENT9 declaration[] = {
        {0, 0, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 8, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        {0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0},
        D3DDECL_END()};
    Shipyard_ImGuiCheck(device->CreateVertexDeclaration(declaration, &vertex_declaration),
                        "ImGui vertex declaration");
    Shipyard_ImGuiCheck(device->CreateStateBlock(D3DSBT_ALL, &saved_state),
                        "ImGui state preservation");
    BuildFontTexture();
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "Shipyard_Xenos";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
}
void Shipyard_ImGuiRendererNewFrame() {
    if (!ImGui::GetIO().Fonts->IsBuilt())
        BuildFontTexture();
}
void Shipyard_ImGuiRender(ImDrawData* data) {
    if (!data || data->DisplaySize.x <= 0 || data->DisplaySize.y <= 0 || !data->TotalVtxCount)
        return;
    IDirect3DDevice9* const device = Shipyard_ImGuiDevice();
    Shipyard_ImGuiBeginNativeDraw();
    saved_state->Capture();
    // ImGui lays out square pixels; an anamorphic SD back buffer is narrower.
    // Framebuffer surfaces are EDRAM-padded or one tile pass, so only the back
    // buffer's description is its size.
    IDirect3DSurface9 *target, *back;
    Shipyard_ImGuiCheck(device->GetRenderTarget(0, &target), "ImGui target");
    Shipyard_ImGuiCheck(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back), "ImGui back buffer");
    data->FramebufferScale = ImVec2(1, 1);
    if (target == back) {
        D3DSURFACE_DESC desc;
        back->GetDesc(&desc);
        data->FramebufferScale = ImVec2(desc.Width / data->DisplaySize.x, desc.Height / data->DisplaySize.y);
    }
    target->Release();
    back->Release();
    Setup(data);
    for (int listIndex = 0; listIndex < data->CmdListsCount; ++listIndex) {
        const ImDrawList* list = data->CmdLists[listIndex];
        vertices.resize(list->VtxBuffer.Size);
        for (int i = 0; i < list->VtxBuffer.Size; ++i) {
            const ImDrawVert& v = list->VtxBuffer[i];
            Vertex& out = vertices[i];
            out.x = v.pos.x;
            out.y = v.pos.y;
            out.u = v.uv.x;
            out.v = v.uv.y;
            out.r = UnitU8((uint8_t)(v.col >> IM_COL32_R_SHIFT));
            out.g = UnitU8((uint8_t)(v.col >> IM_COL32_G_SHIFT));
            out.b = UnitU8((uint8_t)(v.col >> IM_COL32_B_SHIFT));
            out.a = UnitU8((uint8_t)(v.col >> IM_COL32_A_SHIFT));
        }
        for (int i = 0; i < list->CmdBuffer.Size; ++i) {
            const ImDrawCmd& cmd = list->CmdBuffer[i];
            if (cmd.UserCallback) {
                Shipyard_ImGuiInvalidateNativeState();
                if (cmd.UserCallback == ImDrawCallback_ResetRenderState)
                    Setup(data);
                else
                    cmd.UserCallback(list, &cmd);
                Shipyard_ImGuiInvalidateNativeState();
                continue;
            }
            ImVec2 min((cmd.ClipRect.x - data->DisplayPos.x) * data->FramebufferScale.x,
                       (cmd.ClipRect.y - data->DisplayPos.y) * data->FramebufferScale.y);
            ImVec2 max((cmd.ClipRect.z - data->DisplayPos.x) * data->FramebufferScale.x,
                       (cmd.ClipRect.w - data->DisplayPos.y) * data->FramebufferScale.y);
            if (min.x < 0)
                min.x = 0;
            if (min.y < 0)
                min.y = 0;
            float width = data->DisplaySize.x * data->FramebufferScale.x;
            float height = data->DisplaySize.y * data->FramebufferScale.y;
            if (max.x > width)
                max.x = width;
            if (max.y > height)
                max.y = height;
            if (max.x <= min.x || max.y <= min.y || !cmd.ElemCount)
                continue;
            if (cmd.IdxOffset + cmd.ElemCount > (unsigned)list->IdxBuffer.Size ||
                cmd.VtxOffset >= vertices.size())
                Shipyard_ImGuiFatal("ImGui draw bounds", E_FAIL);
            RECT scissor = {(LONG)min.x, (LONG)min.y, (LONG)max.x, (LONG)max.y};
            device->SetScissorRect(&scissor);
            device->SetTexture(0, (IDirect3DTexture9*)cmd.GetTexID());
            Shipyard_ImGuiInvalidateNativeState();
            device->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0,
                                           (UINT)vertices.size() - cmd.VtxOffset, cmd.ElemCount / 3,
                                           list->IdxBuffer.Data + cmd.IdxOffset,
                                           sizeof(ImDrawIdx) == 2 ? D3DFMT_INDEX16 : D3DFMT_INDEX32,
                                           &vertices[cmd.VtxOffset], sizeof(Vertex));
        }
    }
    saved_state->Apply();
    Shipyard_ImGuiInvalidateNativeState();
}
void Shipyard_ImGuiRendererShutdown() {
    Shipyard_ImGuiInvalidateNativeState();
    if (ImGui::GetCurrentContext()) {
        ImGui::GetIO().Fonts->SetTexID(NULL);
        ImGui::GetIO().BackendRendererName = NULL;
        ImGui::GetIO().BackendFlags &= ~ImGuiBackendFlags_RendererHasVtxOffset;
    }
    Shipyard_ImGuiDestroyTexture(font_texture);
    font_texture = NULL;
    if (saved_state) {
        saved_state->Release();
        saved_state = NULL;
    }
    if (vertex_declaration) {
        vertex_declaration->Release();
        vertex_declaration = NULL;
    }
    if (vertex_shader) {
        vertex_shader->Release();
        vertex_shader = NULL;
    }
    if (pixel_shader) {
        pixel_shader->Release();
        pixel_shader = NULL;
    }
    std::vector<Vertex>().swap(vertices);
}
