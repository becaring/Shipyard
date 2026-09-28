#pragma once

namespace Fast {
namespace ShipyardXenos {
// Game replay may defer its initial depth clear until the first operation
// requiring live contents. A certified full opaque first fill can then discard
// retained color before it is restored. Other framebuffer operations fall back.
void BeginGameFramebufferDepthClear();
bool TryDiscardGameFramebufferColor();
// Finish a live target while discarding depth that the next render clears.
void DiscardFramebufferDepth(int id);
// The separate game framebuffer owns depth when the presentation target is color-only.
// Consume this one-shot hint in the next End().
void DiscardPresentationDepthOnEnd();
// Caller will not read the presentation framebuffer image after the next End().
// The direct 720p presentation surface remains intact for Present().
void DiscardPresentationColorOnEnd();
}
}
