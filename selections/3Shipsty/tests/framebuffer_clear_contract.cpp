#include <vector>
#include <cstdio>
#include <cstdlib>
typedef unsigned long DWORD;
enum { D3DCLEAR_TARGET=1, D3DCLEAR_ZBUFFER=2, D3DCLEAR_STENCIL=4 };
struct Framebuffer {
    void* depth;
    bool stored;
    bool depth_stored;
    unsigned samples;
    std::vector<int> tiles;
};
Framebuffer buffers[1];
int active_fb = 0;
bool live_framebuffer = false;
unsigned framebuffer_clear_count = 0;
unsigned framebuffer_depth_restore_avoided = 0;
unsigned framebuffer_restore_avoided = 0;
unsigned native_clear_requests = 0;
unsigned tiling_begins = 0;
void ClearFramebufferTargets(DWORD) { ++native_clear_requests; }
void BeginFramebuffer() {
    if (live_framebuffer) return;
    Framebuffer& b = buffers[active_fb];
    if (b.tiles.size() > 1) ++tiling_begins;
    else if (!b.stored || (b.depth && !b.depth_stored)) ++native_clear_requests;
    live_framebuffer = true;
}
#include "clear_method.inc"
void reset(bool depth, bool stored, bool depth_stored, unsigned tiles, bool live) {
    buffers[0].depth = depth ? &buffers[0] : 0;
    buffers[0].stored = stored;
    buffers[0].depth_stored = depth_stored;
    buffers[0].samples = 4;
    buffers[0].tiles.assign(tiles, 0);
    live_framebuffer = live;
    framebuffer_clear_count = framebuffer_depth_restore_avoided = framebuffer_restore_avoided = 0;
    native_clear_requests = tiling_begins = 0;
}
int main() {
    reset(true, false, false, 1, false); Clear(true, true);
    if (native_clear_requests != 1 || framebuffer_clear_count != 1) return 1;
    reset(true, true, true, 1, false); Clear(true, true);
    if (native_clear_requests != 1 || framebuffer_depth_restore_avoided != 4 ||
        framebuffer_restore_avoided != 4 || buffers[0].stored) return 2;
    reset(true, true, true, 1, true); Clear(true, true);
    if (native_clear_requests != 1) return 3;
    reset(true, true, true, 1, false); Clear(true, false);
    if (native_clear_requests != 1 || !buffers[0].stored || !buffers[0].depth_stored) return 4;
    reset(true, true, true, 1, false); Clear(false, true);
    if (native_clear_requests != 2 || !buffers[0].stored || buffers[0].depth_stored) return 5;
    reset(true, false, false, 2, false); Clear(true, true);
    if (native_clear_requests != 1 || tiling_begins != 1) return 6;
    reset(false, false, false, 1, false); Clear(true, true);
    if (native_clear_requests != 2) return 7;
    reset(true, false, false, 1, false); Clear(false, false);
    if (native_clear_requests || framebuffer_clear_count) return 8;
    std::puts("framebuffer full/partial/retained/tiled clear counts PASS");
    return 0;
}
