#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <set>
#include <unordered_map>
#include <utility>
#include "fast3d/depth_samples.h"
#include "xenos/depth_samples.h"
namespace ShipyardXenos = Fast::ShipyardXenos;
typedef Fast::ShipyardXenos::DepthSample TestDepthSample;
typedef Shipyard::Fast3D::DepthSamples<TestDepthSample> TestDepthSamples;

static size_t allocations;
void* operator new(size_t bytes) { ++allocations; if (void* p = std::malloc(bytes)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
struct hash_pair_ff {
    size_t operator()(const std::pair<float, float>& p) const {
        return std::hash<float>()(p.first) ^ (std::hash<float>()(p.second) << 1);
    }
};
typedef uint32_t DWORD;
typedef int32_t LONG;
struct POINT { LONG x, y; };
struct RECT { LONG left, top, right, bottom; };
struct D3DLOCKED_RECT { void* pBits; };
struct XGTEXTURE_DESC { unsigned Format; };
const unsigned D3DLOCK_READONLY = 1;
const int E_FAIL = -1, FALSE = 0;
struct Trace {
    unsigned validate, finish, begin, lock, unlock, untile, busy, initialized;
    bool operator==(const Trace& rhs) const { return std::memcmp(this, &rhs, sizeof(*this)) == 0; }
} trace;
struct IDirect3DTexture9 {
    unsigned seed;
    int IsBusy() { ++trace.busy; return seed & 1; }
    int LockRect(unsigned, D3DLOCKED_RECT* lock, const void*, unsigned flags) {
        assert(flags == D3DLOCK_READONLY); ++trace.lock; lock->pBits = this; return 0;
    }
    void UnlockRect(unsigned) { ++trace.unlock; }
};
struct Framebuffer {
    IDirect3DTexture9* depth_samples[1];
    uint32_t width, height, samples;
    bool depth_stored;
} buffers[2];
struct Published { IDirect3DTexture9* texture; int framebuffer; uint32_t width, height; } published_depth;
struct Timing {
    uint64_t depthReadTicks, depthLockTicks;
    unsigned depthReadCalls, depthLockCalls, depthSamples;
    unsigned depthBusyCalls, depthBusy1080Calls, depthIdleCalls, depthIdle1080Calls;
} timing_counts;
bool live_framebuffer, timing_enabled = true, detailed_timing_enabled = true;
int active_fb;
struct NativeTimingScope {
    NativeTimingScope(uint64_t& ticks, unsigned& calls) { ++ticks; ++calls; }
};
void ValidateFramebuffer(int id) { assert(id >= 0 && id < 2); ++trace.validate; }
void FinishFramebuffer() {
    ++trace.finish;
    if (live_framebuffer) buffers[active_fb].depth_stored = true;
    live_framebuffer = false;
}
void BeginFramebuffer() { ++trace.begin; live_framebuffer = true; }
void Shipyard_Fatal(const char*, int) { assert(false); }
void Shipyard_Check(int status, const char*) { assert(status == 0); }
void Shipyard_Log(const char*, ...) { ++trace.initialized; }
void XGGetTextureDesc(IDirect3DTexture9*, unsigned, XGTEXTURE_DESC* desc) { desc->Format = 24; }
unsigned XGGetGpuFormat(unsigned format) { return format; }
void XGUntileTextureLevel(unsigned width, unsigned height, unsigned, unsigned, unsigned, DWORD* value,
                          size_t stride, const POINT* destination, void* pixels, const RECT* source) {
    assert(stride == sizeof(DWORD) && destination->x == 0 && destination->y == 0);
    assert(source->left >= 0 && source->right <= int(width) && source->top >= 0 && source->bottom <= int(height));
    auto texture = static_cast<IDirect3DTexture9*>(pixels);
    *value = (texture->seed * 2654435761u) ^ (unsigned(source->left) * 91771u) ^ (unsigned(source->top) * 7919u);
    ++trace.untile;
}
std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
ReferencePixelDepth(int id, const std::set<std::pair<float, float> >& coords) {
    ValidateFramebuffer(id);
    std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff> result;
    if (coords.empty())
        return result;
    NativeTimingScope depthReading(timing_counts.depthReadTicks, timing_counts.depthReadCalls);
    if (timing_enabled)
        timing_counts.depthSamples += (unsigned)coords.size();
    bool resume = false;
    IDirect3DTexture9* depth_texture;
    uint32_t width, height;
    if (published_depth.texture && published_depth.framebuffer == id) {
        // The previous update's final replay. Later replays of this update
        // never wrote this texture, so no framebuffer work is needed; the lock
        // waits only for that replay's depth resolve.
        depth_texture = published_depth.texture;
        width = published_depth.width;
        height = published_depth.height;
    } else {
        resume = live_framebuffer;
        FinishFramebuffer();
        Framebuffer& b = buffers[id];
        // A caller may read depth before its next draw after switching targets.
        // Xenos retains actual depth fragments regardless of the draw-side hint.
        if (!b.depth_samples[0])
            Shipyard_Fatal("depth readback unavailable", E_FAIL);
        if (!b.depth_stored) {
            // A depth query can precede the first draw after an MSAA or resolution
            // change. Initialize real GPU color/depth storage through the same
            // clear/resolve path used for the first draw, then read sample zero.
            int previous = active_fb;
            active_fb = id;
            BeginFramebuffer();
            FinishFramebuffer();
            active_fb = previous;
            Shipyard_Log("initialized depth before first draw: framebuffer=%d samples=%u", id,
                         b.samples);
        }
        depth_texture = b.depth_samples[0];
        width = b.width;
        height = b.height;
    }
    if (timing_enabled && detailed_timing_enabled) {
        // IsBusy is a nonblocking state sample. Do not pre-wait here: that
        // would change the LockRect schedule being measured.
        const bool busy = depth_texture->IsBusy() != FALSE;
        const bool is1080 = width == 1920 && height == 1080;
        if (busy) {
            ++timing_counts.depthBusyCalls;
            if (is1080)
                ++timing_counts.depthBusy1080Calls;
        } else {
            ++timing_counts.depthIdleCalls;
            if (is1080)
                ++timing_counts.depthIdle1080Calls;
        }
    }
    D3DLOCKED_RECT lock;
    {
        NativeTimingScope depthLock(timing_counts.depthLockTicks, timing_counts.depthLockCalls);
        Shipyard_Check(depth_texture->LockRect(0, &lock, NULL, D3DLOCK_READONLY), "depth LockRect");
    }
    XGTEXTURE_DESC desc;
    XGGetTextureDesc(depth_texture, 0, &desc);
    // The caller asks for individual occlusion samples. Let the SDK address
    // those texels directly instead of untiling an entire framebuffer.
    // The source rectangle retains texture packing and the bottom-left contract.
    for (std::set<std::pair<float, float> >::const_iterator it = coords.begin(); it != coords.end();
         ++it) {
        int x = (int)it->first, y = (int)it->second;
        if (x < 0 || y < 0 || x >= (int)width || y >= (int)height) {
            result[*it] = 65532;
            continue;
        }
        DWORD value;
        POINT destination = {0, 0};
        RECT source = {x, (LONG)height - 1 - y, x + 1, (LONG)height - y};
        XGUntileTextureLevel(width, height, 0, XGGetGpuFormat(desc.Format), 0, &value,
                             sizeof(value), &destination, lock.pBits, &source);
        // Match the Fast3D D3D11 backend's sample-zero read for multisampled depth.
        result[*it] = (uint16_t)(((value >> 8) / 16777215.0) * 65532.0);
    }
    depth_texture->UnlockRect(0);
    if (resume)
        BeginFramebuffer();
    return result;
}
// SHARED NATIVE DEPTH

IDirect3DTexture9 target = {57}, published = {98};
void Setup(unsigned mode) {
    trace = {}; timing_counts = {};
    active_fb = int(mode & 1);
    buffers[0] = { {&target}, 1280, 720, 1, (mode & 2) != 0 };
    buffers[1] = { {&target}, 1920, 1080, 4, (mode & 2) != 0 };
    published_depth = {(mode & 4) ? &published : nullptr, int(mode & 1), 640, 480};
    live_framebuffer = (mode & 8) != 0;
}
unsigned batchReads, batchSamples, revision;
uint16_t Value(float x, float y) { return uint16_t(int(x * 13 + y * 7) + revision * 17); }
void ReadFixture(int id, TestDepthSample* samples, size_t count) {
    assert(id == 2 && count <= 64); ++batchReads; batchSamples += unsigned(count);
    for (size_t i = 0; i < count; ++i) samples[i].depth = Value(samples[i].x, samples[i].y);
}
int main() {
    unsigned compared = 0;
    for (unsigned mode = 0; mode < 16; ++mode) {
        for (unsigned count = 0; count <= 160; ++count) {
            std::set<std::pair<float, float>> coords;
            TestDepthSample samples[160];
            for (unsigned i = 0; i < count; ++i) {
                float x = float(int(i * 127 % 2300) - 100) + 0.25f;
                float y = float(int(i * 167 % 1250) - 90) + 0.5f;
                coords.insert({x, y}); samples[i] = {x, y, 0};
            }
            Setup(mode);
            auto expected = ReferencePixelDepth(int(mode & 1), coords);
            Trace before = trace;
            Timing oldTiming = timing_counts;
            bool oldLive = live_framebuffer;
            Setup(mode);
            auto mapResult = PixelDepth(int(mode & 1), coords);
            assert(mapResult == expected && trace == before && live_framebuffer == oldLive);
            assert(std::memcmp(&oldTiming, &timing_counts, sizeof(oldTiming)) == 0);
            Setup(mode);
            size_t allocated = allocations;
            PixelDepthSamples(int(mode & 1), samples, count);
            assert(allocations == allocated && trace == before && live_framebuffer == oldLive);
            assert(std::memcmp(&oldTiming, &timing_counts, sizeof(oldTiming)) == 0);
            for (unsigned i = 0; i < count; ++i) {
                assert(samples[i].depth == expected.at({samples[i].x, samples[i].y})); ++compared;
            }
        }
    }
    TestDepthSamples cache;
    for (unsigned frame = 0; frame < 1000; ++frame) {
        cache.Clear(); revision = frame; batchReads = batchSamples = 0;
        const size_t allocated = allocations;
        for (unsigned i = 0; i < 33; ++i) { cache.Prepare(float(i), float(i + 1)); cache.Prepare(float(i), float(i + 1)); }
        assert(cache.Read(2, 0, 1, ReadFixture) == Value(0, 1));
        assert(batchReads == 1 && batchSamples == 33);
        ++revision;
        for (unsigned i = 0; i < 33; ++i)
            assert(cache.Read(2, float(i), float(i + 1), ReadFixture) == uint16_t(Value(float(i), float(i + 1)) - 17));
        assert(batchReads == 1 && allocations == allocated);
        assert(cache.Read(2, 55, 56, ReadFixture) == Value(55, 56));
        assert(batchReads == 2 && batchSamples == 34 && allocations == allocated);
    }
    cache.Clear(); revision = 99; batchReads = batchSamples = 0;
    for (unsigned i = 0; i < 150; ++i) cache.Prepare(float(i), float(i));
    for (unsigned i = 0; i < 150; ++i) assert(cache.Read(2, float(i), float(i), ReadFixture) == Value(float(i), float(i)));
    assert(batchReads == 3 && batchSamples == 150);
    cache.Clear(); batchReads = batchSamples = 0;
    cache.Prepare(-0.0f, 0); cache.Prepare(0, -0.0f);
    assert(cache.Read(2, 0, 0, ReadFixture) == Value(0, 0));
    assert(batchReads == 1 && batchSamples == 1);
    printf("%u native depth samples matched; 1000 allocation-free sun/light batches and overflow passed\n", compared);
}
