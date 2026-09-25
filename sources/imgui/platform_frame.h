#pragma once

namespace Shipyard {
namespace ImGuiPlatform {

struct Services {
    double (*now)();
    void (*prepareInput)();
    const char* backendName;
};

void Init(const Services& services);
void Shutdown();
void NewFrame(unsigned width, unsigned height, bool menuVisible);
void DrawCursor();

} // namespace ImGuiPlatform
} // namespace Shipyard
