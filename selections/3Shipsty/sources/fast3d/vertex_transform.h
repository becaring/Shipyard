#pragma once

#include <float.h>
#include <stddef.h>
#include <stdint.h>

namespace Shipyard { namespace Fast3D {

#ifdef _XBOX
// Read FPSCR RN without _controlfp's kernel call or FPSCR writes.
static __declspec(naked) bool NearestRounding() {
    __asm {
        mffs fp0
        stfd fp0,-8(sp)
        lwz r3,-4(sp)
        rlwinm r3,r3,0,30,31
        cntlzw r3,r3
        srwi r3,r3,5
        blr
    }
}
#else
inline bool NearestRounding() { return (_controlfp(0,0)&_MCW_RC)==_RC_NEAR; }
#endif

inline bool VertexSourceDisjoint(const void* source,size_t bytes,const void* state,size_t stateBytes) {
    const uintptr_t a=(uintptr_t)source,b=(uintptr_t)state;
    const uintptr_t end=a+bytes,stateEnd=b+stateBytes;
    return end>=a && stateEnd>=b && (end<=b || stateEnd<=a);
}

// Input records begin with signed x/y/z halfwords at a 16-byte stride.
// Output rows must be 16-byte aligned, with stride a multiple of 16.
// An unsupported matrix returns false without writing output.
struct TransformPhaseHook {
    void (*enterVertices)(void* context);
    void* context;
};
bool TransformPositions(const void* vertices,size_t count,const float matrix[4][4],
                        void* output,bool normalAspect=false,size_t outputStride=16);
// The sampled diagnostic uses a separate entrypoint so the ordinary VMX
// function retains its original register allocation and call-free loop.
bool TransformPositionsProfiled(const void* vertices,size_t count,const float matrix[4][4],
                                void* output,bool normalAspect,size_t outputStride,
                                const TransformPhaseHook* phaseHook);

} }
