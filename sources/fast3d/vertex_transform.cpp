// Adapted from SoH360/platform/xbox360/fast3d_transform.cpp.
// Recorded donor SHA256: 5f0968d08160776db6c0c501f29e1a79879e5d22860fed2241db93aefdd2c29a.
#include "fast3d/vertex_transform.h"
#include "fast3d/vmx_load.h"

namespace Shipyard {
namespace Fast3D {

static __forceinline __vector4 TransformInvalid(__vector4 bits, __vector4 sign, __vector4 lower,
                                                __vector4 upper, __vector4 zero) {
    const __vector4 magnitude = __vandc(bits, sign);
    return __vor(__vcmpgtuw(magnitude, upper),
                 __vandc(__vcmpgtuw(lower, magnitude), __vcmpequw(magnitude, zero)));
}

// Explicit VMX multiply/add pairs retain the scalar operation order.
#pragma fp_contract(off)
bool TransformPositions(const void* vertices, size_t count, const float matrix[4][4], void* output,
                        bool normalAspect, size_t outputStride, const TransformPhaseHook* phaseHook) {
    // VMX flushes subnormals. Integer inputs and bounded coefficients keep
    // nonzero products normal and four-term sums finite; all other matrices
    // retain the scalar path. normalAspect tightens the lower bound.
    static const __declspec(align(16)) uint32_t limits[4][4] = {
        {0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u},
        {0x0c000000u, 0x0c000000u, 0x0c000000u, 0x0c000000u},
        {0x75ffffffu, 0x75ffffffu, 0x75ffffffu, 0x75ffffffu},
        {0x12800000u, 0x12800000u, 0x12800000u, 0x12800000u}};
    const __vector4 sign = __lvx(limits, 0), lower = __lvx(limits, normalAspect ? 48 : 16),
                    upper = __lvx(limits, 32), zero = __vspltisw(0);
    const __vector4 m0 = Detail::LoadUnaligned(matrix[0]), m1 = Detail::LoadUnaligned(matrix[1]);
    const __vector4 m2 = Detail::LoadUnaligned(matrix[2]), m3 = Detail::LoadUnaligned(matrix[3]);
    const __vector4 invalid = __vor(__vor(TransformInvalid(m0, sign, lower, upper, zero),
                                          TransformInvalid(m1, sign, lower, upper, zero)),
                                    __vor(TransformInvalid(m2, sign, lower, upper, zero),
                                          TransformInvalid(m3, sign, lower, upper, zero)));
    unsigned condition;
    __vcmpequwR(invalid, zero, &condition);
    if (!(condition & 0x80))
        return false;
    if (phaseHook) phaseHook->enterVertices(phaseHook->context);
    for (size_t i = 0; i < count; ++i) {
        const __vector4 position =
            __vcfsx(__vupkhsh(Detail::LoadUnaligned((const unsigned char*)vertices + i * 16)), 0);
        const __vector4 x = __vmulfp(__vspltw(position, 0), m0);
        const __vector4 y = __vmulfp(__vspltw(position, 1), m1);
        const __vector4 z = __vmulfp(__vspltw(position, 2), m2);
        const __vector4 result = __vaddfp(__vaddfp(__vaddfp(x, y), z), m3);
        __stvx(result, output, (int)(i * outputStride));
    }
    return true;
}

} // namespace Fast3D
} // namespace Shipyard
