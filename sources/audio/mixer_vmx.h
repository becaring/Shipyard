/* Exact VMX lanes for N64 audio mixing: saturated add or subtract of 16-bit
 * samples, and stereo interleave. Each returns 0, so the caller's scalar
 * loop does the work, for empty, unaligned or partially overlapping buffers
 * (scalar loops carry sample-to-sample feedback there) and off Xenon. */
#ifndef SHIPYARD_AUDIO_MIXER_VMX_H
#define SHIPYARD_AUDIO_MIXER_VMX_H
#include <stdint.h>
#if defined(_XBOX) && defined(_M_PPC)
#include <vectorintrinsics.h>
#endif

static int ShipyardAudio_SaturatedMix(int16_t* out, const int16_t* in, int bytes, int subtract) {
#if defined(_XBOX) && defined(_M_PPC)
    uintptr_t output = (uintptr_t)out, input = (uintptr_t)in;
    int offset;
    if (bytes <= 0 || ((output | input | (uintptr_t)bytes) & 15)) return 0;
    if (output != input && output < input + bytes && input < output + bytes) return 0;
    for (offset = 0; offset < bytes; offset += 16) {
        __vector4 left = __lvx(out, offset);
        __vector4 right = __lvx(in, offset);
        __stvx(subtract ? __vsubshs(left, right) : __vaddshs(left, right), out, offset);
    }
    return 1;
#else
    (void)out; (void)in; (void)bytes; (void)subtract;
    return 0;
#endif
}

/* bytes is the size of each input channel; out receives 2 * bytes. */
static int ShipyardAudio_Interleave(int16_t* out, const int16_t* left, const int16_t* right, int bytes) {
#if defined(_XBOX) && defined(_M_PPC)
    uintptr_t output = (uintptr_t)out, l = (uintptr_t)left, r = (uintptr_t)right;
    int offset;
    if (bytes <= 0 || ((output | l | r | (uintptr_t)bytes) & 15)) return 0;
    if ((output < l + bytes && l < output + 2 * bytes) ||
        (output < r + bytes && r < output + 2 * bytes)) return 0;
    for (offset = 0; offset < bytes; offset += 16) {
        __vector4 a = __lvx(left, offset), b = __lvx(right, offset);
        __stvx(__vmrghh(a, b), out, offset * 2);
        __stvx(__vmrglh(a, b), out, offset * 2 + 16);
    }
    return 1;
#else
    (void)out; (void)left; (void)right; (void)bytes;
    return 0;
#endif
}
#endif
