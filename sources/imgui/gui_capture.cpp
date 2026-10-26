#include "gui_capture.h"
#include "shipyard_imgui_xenos.h"
#include "overlap/gui_frame_cache.h"

namespace Fast { namespace ShipyardXenos {
void RetireExternalTexture(IDirect3DTexture9* texture);
} }

namespace {
Shipyard::Overlap::GuiFrameCache* captureCache;
bool captureSucceeded;
bool suppressCapturedDraw;
uintptr_t capturedGameTexture;
char gameTextureMarker;

void retainGuiTexture(void*, ImTextureID texture) {
    static_cast<IDirect3DTexture9*>(texture)->AddRef();
}
void retireGuiTexture(void*, ImTextureID texture) {
    Fast::ShipyardXenos::RetireExternalTexture(static_cast<IDirect3DTexture9*>(texture));
}
}

void Shipyard_ImGuiBeginCapture(Shipyard::Overlap::GuiFrameCache* cache, bool suppressDraw) {
    captureCache = cache;
    captureSucceeded = false;
    suppressCapturedDraw = suppressDraw;
    capturedGameTexture = 0;
}

bool Shipyard_ImGuiEndCapture() {
    captureCache = NULL;
    return captureSucceeded;
}

bool Shipyard_ImGuiCaptureDrawData(ImDrawData* data) {
    if (!captureCache) return false;
    const Shipyard::Overlap::GuiTextureLifetime textures(NULL, retainGuiTexture, retireGuiTexture);
    captureSucceeded = captureCache->capture(*data, &gameTextureMarker, textures,
        (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0);
    if (!suppressCapturedDraw) {
        for (int i = 0; i != data->CmdListsCount; ++i) {
            ImDrawList* list = data->CmdLists[i];
            for (int j = 0; j != list->CmdBuffer.Size; ++j) {
                ImDrawCmd& cmd = list->CmdBuffer[j];
                if (cmd.TextureId == &gameTextureMarker && !cmd.UserCallback) {
                    cmd.TextureId = reinterpret_cast<ImTextureID>(capturedGameTexture);
                    if (!capturedGameTexture) cmd.ElemCount = 0;
                }
            }
        }
    }
    return suppressCapturedDraw;
}

uintptr_t Shipyard_ImGuiCaptureGameTexture(uintptr_t texture) {
    capturedGameTexture = texture;
    return captureCache ? reinterpret_cast<uintptr_t>(&gameTextureMarker) : texture;
}

void Shipyard_ImGuiDrawCache(Shipyard::Overlap::GuiFrameCache& cache, uintptr_t texture) {
    cache.retargetGame(reinterpret_cast<ImTextureID>(texture));
    Shipyard_ImGuiRender(&cache.drawData());
}
