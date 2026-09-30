#pragma once

namespace Fast {
namespace ShipyardXenos {
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
