#include "fast3d/clip_quantize.h"
#include "fast3d/vmx_load.h"
#include <vectorintrinsics.h>
#include <stdint.h>

using Shipyard::Fast3D::Detail::LoadUnaligned;

#pragma fp_contract(off)
namespace Shipyard { namespace Fast3D {

template<bool IncludeNear> static void ClipVerticesImpl(void* vertices, size_t count) {
    static const __declspec(align(16)) uint32_t constants[5][4] = {
        {0x80000000u,0x80000000u,0x80000000u,0x80000000u},
        {0x7f800000u,0x7f800000u,0x7f800000u,0x7f800000u},
        {1,4,16,0},{1,4,0,0},{2,8,32,0}};
    const __vector4 zero=__vspltisw(0),ones=__vspltisw(-1);
    const __vector4 signBit=__lvx(constants,0),infinity=__lvx(constants,16);
    const __vector4 lowerBits=__lvx(constants,IncludeNear?32:48),upperBits=__lvx(constants,64);
    const __vector4 shift31=__vspltisw(-1),shift24=__vspltisw(-8);
    for(size_t i=0;i<count;++i) {
        unsigned char* vertex=(unsigned char*)vertices+i*ClipVertexStride;
        __vector4 bits=LoadUnaligned(vertex);
        const __vector4 magnitude=__vandc(bits,signBit);
        const __vector4 isZero=__vcmpequw(magnitude,zero);
        const __vector4 isNaN=__vcmpgtuw(magnitude,infinity);
        bits=__vandc(bits,isZero);
        const __vector4 key=__vxor(bits,__vor(signBit,__vsraw(bits,shift31)));
        const __vector4 w=__vspltw(key,3);
        const __vector4 negativeW=__vsel(__vxor(w,ones),signBit,__vspltw(isZero,3));
        const __vector4 unordered=__vor(isNaN,__vspltw(isNaN,3));
        __vector4 flags=__vor(__vand(__vcmpgtuw(negativeW,key),lowerBits),
                             __vand(__vcmpgtuw(key,w),upperBits));
        flags=__vandc(flags,unordered);
        flags=__vor(flags,__vpermwi(flags,0x4e));
        flags=__vor(flags,__vpermwi(flags,0xb1));
        __stvebx(__vslw(flags,shift24),vertex,ClipByteOffset);
    }
}

void ClipVertices(void* vertices, size_t count, bool includeNearPlane) {
    if(includeNearPlane) ClipVerticesImpl<true>(vertices,count);
    else ClipVerticesImpl<false>(vertices,count);
}

template<bool Aligned, bool IncludeNear> static void ClipFiniteVerticesImpl(void* vertices, size_t count) {
    static const __declspec(align(16)) uint32_t constants[4][4]={
        {0x80000000u,0x80000000u,0x80000000u,0x80000000u},
        {1,4,16,0},{1,4,0,0},{2,8,32,0}};
    const __vector4 zero=__vspltisw(0),sign=__lvx(constants,0);
    const __vector4 lowerBits=__lvx(constants,IncludeNear?16:32),upperBits=__lvx(constants,48);
    const __vector4 shift24=__vspltisw(-8);
    for(size_t i=0;i<count;++i) {
        unsigned char* vertex=(unsigned char*)vertices+i*ClipVertexStride;
        const __vector4 bits=Aligned?__lvx(vertex,0):LoadUnaligned(vertex),w=__vspltw(bits,3);
        __vector4 flags=__vor(__vand(__vcmpgtfp(__vxor(w,sign),bits),lowerBits),
                             __vand(__vcmpgtfp(bits,w),upperBits));
        const __vector4 atZero=__vor(__vand(__vcmpgtuw(bits,sign),lowerBits),
                                    __vand(__vcmpgtsw(bits,zero),upperBits));
        flags=__vsel(flags,atZero,__vcmpeqfp(w,zero));
        flags=__vor(flags,__vpermwi(flags,0x4e));
        flags=__vor(flags,__vpermwi(flags,0xb1));
        __stvebx(__vslw(flags,shift24),vertex,ClipByteOffset);
    }
}

void ClipFiniteVertices(void* vertices, size_t count, bool includeNearPlane) {
    if(includeNearPlane) {
        if((uintptr_t)vertices&15) ClipFiniteVerticesImpl<false,true>(vertices,count);
        else ClipFiniteVerticesImpl<true,true>(vertices,count);
    } else {
        if((uintptr_t)vertices&15) ClipFiniteVerticesImpl<false,false>(vertices,count);
        else ClipFiniteVerticesImpl<true,false>(vertices,count);
    }
}

static __forceinline __vector4 QuantizationInvalid(__vector4 bits,__vector4 sign,
                                                   __vector4 limit,__vector4 negativeLimit) {
    return __vandc(__vcmpgtuw(__vandc(bits,sign),limit),__vcmpequw(bits,negativeLimit));
}
bool QuantizeMatrix(const float input[4][4],float output[4][4]) {
    static const __declspec(align(16)) uint32_t limits[3][4]={
        {0x80000000u,0x80000000u,0x80000000u,0x80000000u},
        {0x46ffffffu,0x46ffffffu,0x46ffffffu,0x46ffffffu},
        {0xc7000000u,0xc7000000u,0xc7000000u,0xc7000000u}};
    const __vector4 sign=__lvx(limits,0),limit=__lvx(limits,16),negativeLimit=__lvx(limits,32);
    const __vector4 m0=LoadUnaligned(input[0]),m1=LoadUnaligned(input[1]);
    const __vector4 m2=LoadUnaligned(input[2]),m3=LoadUnaligned(input[3]);
    const __vector4 invalid=__vor(__vor(QuantizationInvalid(m0,sign,limit,negativeLimit),
                                      QuantizationInvalid(m1,sign,limit,negativeLimit)),
                                 __vor(QuantizationInvalid(m2,sign,limit,negativeLimit),
                                      QuantizationInvalid(m3,sign,limit,negativeLimit)));
    unsigned condition;__vcmpequwR(invalid,__vspltisw(0),&condition);
    if(!(condition&0x80))return false;
    __stvx(__vcfsx(__vctsxs(m0,16),16),output,0);
    __stvx(__vcfsx(__vctsxs(m1,16),16),output,16);
    __stvx(__vcfsx(__vctsxs(m2,16),16),output,32);
    __stvx(__vcfsx(__vctsxs(m3,16),16),output,48);
    return true;
}

} }
