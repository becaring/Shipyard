"""Use inline depth requests for Fast3D interpreters with coordinate adjustment.

Adapters select this after auditing their pinned depth methods. Coordinate
conversion stays in the interpreter; the shared Xenos callback owns readback.
"""
import re


def _once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Depth sample source anchor changed: ' + old[:80])
    return text.replace(old, new, 1)


def header(text):
    declarations = [
        (r'^    std::set<std::pair<float,\s*float>\s*>\s+mGetPixelDepthPending;[^\n]*',
         '    Shipyard::Fast3D::DepthSamples<Fast::ShipyardXenos::DepthSample> mXboxDepthSamples;'),
        (r'^    std::unordered_map<std::pair<float,\s*float>,\s*uint16_t,\s*hash_pair_ff>\s+mGetPixelDepthCached;[^\n]*', ''),
    ]
    for pattern, replacement in declarations:
        text, count = re.subn(pattern, replacement, text, flags=re.MULTILINE)
        if count != 1:
            raise ValueError('Depth sample member inventory changed')
    return '#include "fast3d/depth_samples.h"\n#include "xenos/depth_samples.h"\n' + text


def transform(text, expected_resets=2):
    reset = r'    mGetPixelDepthPending\.clear\(\);\s*\n    mGetPixelDepthCached\.clear\(\);'
    text, count = re.subn(reset, '    mXboxDepthSamples.Clear();', text)
    if count != expected_resets:
        raise ValueError('Depth sample reset inventory changed')
    replacements = [
        ('void Interpreter::GetPixelDepthPrepare(float x, float y) {', '''
    AdjustPixelDepthCoordinates(x, y);
    mXboxDepthSamples.Prepare(x, y);
}'''),
        ('uint16_t Interpreter::GetPixelDepth(float x, float y) {', '''
    AdjustPixelDepthCoordinates(x, y);
    return mXboxDepthSamples.Read(mRendersToFb ? mGameFb : 0, x, y,
                                  Fast::ShipyardXenos::GetPixelDepthSamples);
}'''),
    ]
    for signature, body in replacements:
        if text.count(signature) != 1:
            raise ValueError('Depth sample method inventory changed')
        start = text.index(signature)
        end = text.index('\n}', start) + 2
        original = text[start:end]
        if original.count('AdjustPixelDepthCoordinates(x, y);') != 1:
            raise ValueError('Depth sample coordinate adjustment changed')
        text = text[:start] + signature + body + text[end:]
    if 'mGetPixelDepthPending' in text or 'mGetPixelDepthCached' in text:
        raise ValueError('Unadapted depth sample access')
    return text
