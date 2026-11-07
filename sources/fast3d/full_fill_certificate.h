#pragma once

#include <stdint.h>

namespace Shipyard {
namespace Fast3D {

// Proves that a FILL rectangle overwrites every pixel of the target, so the
// target's retained color need not be restored first
// (ShipyardXenos::TryDiscardGameFramebufferColor).
//
// left/right/top/bottom are the rectangle's NDC edges, computed with the
// consumer's own GfxDrawRectangle arithmetic after FILL's inclusive edge.
// viewport is that rectangle's viewport after AdjustVIewportOrScissor, scissor
// the RDP scissor SetScissor will receive, and clip0-3 the reserved rectangle
// vertices' current clip_rej bits.
inline bool FullFillCovers(uint32_t targetWidth, uint32_t targetHeight, float left, float right,
                           float top, float bottom, int viewportX, int viewportY, int viewportWidth,
                           int viewportHeight, float scissorX, float scissorY, float scissorWidth,
                           float scissorHeight, uint8_t clip0, uint8_t clip1, uint8_t clip2,
                           uint8_t clip3) {
    if (targetWidth == 0 || targetHeight == 0 || targetWidth > 16384 || targetHeight > 16384)
        return false;
    // GfxSpTri1 tests these reserved-vertex rejection bits even for
    // rectangles. An omitted half would not overwrite the whole target.
    if ((clip0 & clip1 & clip3) || (clip1 & clip2 & clip3))
        return false;
    if (viewportX > 0 || viewportY > 0 || viewportWidth < 0 || viewportHeight < 0 ||
        viewportX + viewportWidth < (int)targetWidth || viewportY + viewportHeight < (int)targetHeight)
        return false;
    // SetScissor takes integers. Check the same truncated bounds it will use.
    if (!(scissorX >= -65536.0f && scissorX <= 65536.0f && scissorY >= -65536.0f &&
          scissorY <= 65536.0f && scissorWidth >= 0.0f && scissorWidth <= 65536.0f &&
          scissorHeight >= 0.0f && scissorHeight <= 65536.0f))
        return false;
    const int sx = (int)scissorX, sy = (int)scissorY;
    const int sw = (int)scissorWidth, sh = (int)scissorHeight;
    if (sx > 0 || sy > 0 || sx + sw < (int)targetWidth || sy + sh < (int)targetHeight)
        return false;
    return left <= -1.0f && right >= 1.0f && top >= 1.0f && bottom <= -1.0f;
}

// Fast3D's upstream rectangle: U10.2 edges, AdjXForAspectRatio's 4:3 X
// compression, and a default viewport covering the whole game target.
inline bool FullGameFillCovers(uint32_t nativeWidth, uint32_t nativeHeight, uint32_t targetWidth,
                               uint32_t targetHeight, int32_t ulx, int32_t uly, int32_t lrx,
                               int32_t lry, float scissorX, float scissorY, float scissorWidth,
                               float scissorHeight, uint8_t clip0, uint8_t clip1, uint8_t clip2,
                               uint8_t clip3) {
    if (nativeWidth < 2 || nativeHeight < 2 || targetWidth == 0 || targetHeight == 0)
        return false;
    // Match GfxDrawRectangle and AdjXForAspectRatio's operation order.
    const float targetAspect = (float)targetWidth / (float)targetHeight;
    const float left = ((ulx / (4.0f * (nativeWidth / 2)) - 1.0f) * (4.0f / 3.0f)) / targetAspect;
    const float right = ((lrx / (4.0f * (nativeWidth / 2)) - 1.0f) * (4.0f / 3.0f)) / targetAspect;
    const float top = -(uly / (4.0f * (nativeHeight / 2))) + 1.0f;
    const float bottom = -(lry / (4.0f * (nativeHeight / 2))) + 1.0f;
    return FullFillCovers(targetWidth, targetHeight, left, right, top, bottom, 0, 0, (int)targetWidth,
                          (int)targetHeight, scissorX, scissorY, scissorWidth, scissorHeight, clip0,
                          clip1, clip2, clip3);
}

} // namespace Fast3D
} // namespace Shipyard
