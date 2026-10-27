// Capture only during a render-owned handoff with game and synthesis idle.
// Later replays of the same packet redraw the capture with their own game image.
#ifndef SHIPYARD_IMGUI_GUI_CAPTURE_H
#define SHIPYARD_IMGUI_GUI_CAPTURE_H
#include "imgui.h"
#include "overlap/gui_frame_cache.h"
#include <stdint.h>

void Shipyard_ImGuiBeginCapture(Shipyard::Overlap::GuiFrameCache* cache, bool suppressDraw = true);
bool Shipyard_ImGuiEndCapture();
// The GUI backend calls these in place of rendering and of naming the game image.
bool Shipyard_ImGuiCaptureDrawData(ImDrawData* data);
uintptr_t Shipyard_ImGuiCaptureGameTexture(uintptr_t texture);
void Shipyard_ImGuiDrawCache(Shipyard::Overlap::GuiFrameCache& cache, uintptr_t texture);

namespace Shipyard {
namespace Overlap {
// Capture policy for overlap/runtime.h.
struct ImGuiCapture {
    typedef GuiFrameCache Cache;
    static void Begin(Cache* cache, bool suppressDraw) { Shipyard_ImGuiBeginCapture(cache, suppressDraw); }
    static bool End() { return Shipyard_ImGuiEndCapture(); }
    static void Draw(Cache& cache, uintptr_t texture) { Shipyard_ImGuiDrawCache(cache, texture); }
};
}
}

#endif
