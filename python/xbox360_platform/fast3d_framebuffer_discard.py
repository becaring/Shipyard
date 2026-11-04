"""Interpreter wiring for the Xenos framebuffer discard hints (xenos/depth_discard.h).

The backend validates every hint against its own target state and falls back
to an ordinary restore or resolve; the interpreter supplies what only it knows:
which depth the next render clears, which presentation attachments nothing
reads after End, and that the frame's first color operation overwrites every
pixel of the game target (fast3d/full_fill_certificate.h).

The interpreter declares `bool mXboxDiscardGameDepth` (final_depth with
flag=True) and `bool mXboxCanDiscardGameColor` (game_fill), both
initialized false. game_fill needs SHIPYARD_XENOS_DEFERRED_FRAMEBUFFERS for
the deferred depth clear to pay off, and mXboxPipelineKey for its native call.
"""


def _once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Framebuffer discard anchor drift: ' + old.strip()[:80])
    return text.replace(old, new)


def _method(text, signature):
    if text.count(signature) != 1:
        raise ValueError('Framebuffer discard anchor drift: ' + signature)
    start = text.index(signature)
    return start, text.index('\n}', start) + 2


def _enter(text, signature, statement):
    """Insert `statement` at the top of the one method starting with `signature`."""
    start, end = _method(text, signature)
    body = text.index(' {\n', start) + 3
    if body > end:
        raise ValueError('Framebuffer discard anchor drift: ' + signature)
    return text[:body] + statement + text[body:]


def final_depth(text, *, anchor, flag=True):
    """Finish the game target without storing depth that the next render clears.

    `anchor` ends with Run's switch to the presentation target; the discard
    goes before its last line. With `flag`, the replay owner opts in per Run
    through mXboxDiscardGameDepth; without it the consumer never reads game
    depth after Run, so every Run discards it.
    """
    start, end = _method(text, 'void Interpreter::Run(')
    body = text[start:end]
    head, newline, last = anchor.rpartition('\n')
    indent = last[:len(last) - len(last.lstrip())]
    discard = 'Fast::ShipyardXenos::DiscardFramebufferDepth(mGameFb);\n'
    if flag:
        discard = 'if (mXboxDiscardGameDepth) ' + discard + indent + 'mXboxDiscardGameDepth = false;\n'
    if 'DiscardFramebufferDepth(' in body:
        raise ValueError('Framebuffer discard anchor drift: Run already discards depth')
    body = _once(body, anchor, head + newline + indent + discard + last)
    return '#include "xenos/depth_discard.h"\n' + text[:start] + body + text[end:]


PUBLICATION = """void Interpreter::XboxPublishGameDepth() {
    Fast::ShipyardXenos::PublishFramebufferDepth(mRendersToFb ? mGameFb : 0);
}

void Interpreter::XboxRetireGameDepth() {
    Fast::ShipyardXenos::RetireFramebufferDepth();
}

"""


def depth_publication(text):
    """Define XboxPublishGameDepth/XboxRetireGameDepth beside GetPixelDepth.

    The replay owner publishes the game target's depth after an update's final
    replay and samples it once the next update's first replay is recorded, so
    the lock never waits for the GPU to drain; it retires the publication after
    that read and whenever it leaves the replay path. The interpreter header
    declares both methods.
    """
    signature = 'uint16_t Interpreter::GetPixelDepth(float x, float y) {'
    return _once(text, signature, PUBLICATION + signature)


def presentation(text, *, depth, color):
    """In EndFrame, tell End that nothing reads presentation depth or color afterwards."""
    return _enter(text, 'void Interpreter::EndFrame()',
                  '    if (%s)\n        Fast::ShipyardXenos::DiscardPresentationDepthOnEnd();\n'
                  '    if (%s)\n        Fast::ShipyardXenos::DiscardPresentationColorOnEnd();\n' % (depth, color))


FILL = '''    // The backend also requires a pending, idle, single-sample game target.
    // This proves the frame's first color operation overwrites every pixel.
    if (mXboxCanDiscardGameColor) {
        const LoadedVertex* xboxCorners = &mRsp->loaded_vertices[MAX_VERTICES];
%(viewport)s
        const bool xboxFullOpaqueFill = !mFbActive && mMsaaLevel == 1 && mBufVboLen == 0 &&
            mBufVboNumTris == 0 && mode == G_CYC_FILL &&
            mRdp->other_mode_l == (G_AC_NONE | G_ZS_PIXEL | G_RM_NOOP | G_RM_NOOP2) &&
            mRdp->fill_color.a == 255 && !mRdp->grayscale &&
            !XboxTrianglePipelineTraits::ShaderPresent(*this) &&
            Shipyard::Fast3D::FullFillCovers(mCurDimensions.width, mCurDimensions.height,
                %(left)s, %(right)s,
                %(top)s, %(bottom)s,
                xboxViewport.x, xboxViewport.y, (int)xboxViewport.width, (int)xboxViewport.height,
                (float)mRdp->scissor.x, (float)mRdp->scissor.y,
                (float)mRdp->scissor.width, (float)mRdp->scissor.height,
                xboxCorners[0].clip_rej, xboxCorners[1].clip_rej,
                xboxCorners[2].clip_rej, xboxCorners[3].clip_rej);
        if (xboxFullOpaqueFill)
            Fast::ShipyardXenos::TryDiscardGameFramebufferColor();
        // A partial first fill or backend rejection cannot be retried.
        mXboxCanDiscardGameColor = false;
    }

'''


def game_fill(text, *, clear, eligible, left, right, top, bottom, viewport):
    """Defer Run's depth-only game clear; a certified full opaque first fill discards retained color.

    `clear` is Run's depth-only clear of the game target and `eligible` the
    frame condition (single-sample, whole-target game viewport). left/right/
    top/bottom are C++ expressions of the fill's final ulx/uly/lrx/lry using
    the consumer's own GfxDrawRectangle arithmetic; `viewport` declares
    `xboxViewport`, that rectangle's viewport after AdjustVIewportOrScissor.
    Any triangle (rectangles included) or framebuffer switch or copy first
    revokes eligibility.
    """
    if '\n' in clear:
        raise ValueError('Framebuffer discard clear must be one statement line')
    start, end = _method(text, 'void Interpreter::Run(')
    indent = clear[:len(clear) - len(clear.lstrip())]
    body = _once(text[start:end], clear + '\n',
                 indent + 'mXboxCanDiscardGameColor = ' + eligible + ';\n' +
                 indent + 'if (mXboxCanDiscardGameColor) {\n' +
                 indent + '    mXboxPipelineKey.valid = false;\n' +
                 indent + '    Fast::ShipyardXenos::BeginGameFramebufferDepthClear();\n' +
                 indent + '} else {\n    ' + clear + '\n' + indent + '}\n')
    text = text[:start] + body + text[end:]
    for signature in ('void Interpreter::GfxSpTri1(', 'void Interpreter::SetFrameBuffer(',
                      'void Interpreter::CopyFrameBuffer('):
        text = _enter(text, signature, '    mXboxCanDiscardGameColor = false;\n')
    start, end = _method(text, 'void Interpreter::GfxDpFillRectangle(')
    body = _once(text[start:end], '    for (int i = MAX_VERTICES; i < MAX_VERTICES + 4; i++) {',
                 FILL % dict(viewport=viewport, left=left, right=right, top=top, bottom=bottom) +
                 '    for (int i = MAX_VERTICES; i < MAX_VERTICES + 4; i++) {')
    text = text[:start] + body + text[end:]
    return '#include "fast3d/full_fill_certificate.h"\n#include "xenos/depth_discard.h"\n' + text
