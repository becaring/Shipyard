"""Material replay memos: decoded modes and texture-cache nodes reused across draws and replays.

Each memo is a Shipyard::Fast3D::MaterialMemo (storage outside Xenon's small
TLS section). A combiner pointer lives until mColorCombinerPool.clear() and a
cache node until a map erase or clear; every such site advances a process-wide
epoch that the keys include, so a clear on another thread or a recycled
interpreter address never returns a stale pointer. Misses run the original
code unchanged, including its native calls and flushes.
"""

import hashlib
import re

MODE_START = '    uint64_t cc_id = mRdp->combine_mode;\n'
MODE_END = '    ColorCombiner* comb = LookupOrCreateColorCombiner(key);\n'

HELPERS = '''// SHIPYARD MATERIAL REPLAY BEGIN
// Epochs are process-wide: invalidation may run on any thread.
static unsigned sXboxCombinerEpoch = 1;
static unsigned sXboxTextureEpoch = 1;
struct XboxModeKey {
    const Interpreter* owner;
    unsigned epoch;
    uint64_t combine;
    uint32_t low, high;
    int64_t shader;
    unsigned flags;
    bool operator==(const XboxModeKey& rhs) const {
        return owner == rhs.owner && epoch == rhs.epoch && combine == rhs.combine && low == rhs.low &&
            high == rhs.high && shader == rhs.shader && flags == rhs.flags;
    }
};
struct XboxModeValue {
    ColorCombiner* comb;
%(values)s};
static __declspec(thread) Shipyard::Fast3D::MaterialMemo<XboxModeKey, XboxModeValue, 256> sXboxModes;
// Every input of the decoded mode: combine, other modes, grayscale, the
// shader stack top and the TMEM mask/blend flags.
static __forceinline XboxModeKey XboxCaptureMode(const Interpreter& gfx) {
    const RDP& r = *gfx.mRdp;
    const bool shader = XboxTrianglePipelineTraits::ShaderPresent(gfx);
    XboxModeKey key = { &gfx, sXboxCombinerEpoch, r.combine_mode, r.other_mode_l, r.other_mode_h,
        shader ? (int64_t)XboxTrianglePipelineTraits::ShaderIdentity(gfx) : 0,
        unsigned(r.grayscale) | (unsigned(shader) << 1) | (unsigned(r.loaded_texture[0].masked) << 2) |
        (unsigned(r.loaded_texture[1].masked) << 3) | (unsigned(r.loaded_texture[0].blended) << 4) |
        (unsigned(r.loaded_texture[1].blended) << 5) };
    return key;
}
static __forceinline unsigned XboxHashMode(const XboxModeKey& key) {
    return Shipyard::Fast3D::HashMaterialKey(unsigned(key.combine) ^ unsigned(key.combine >> 32) ^ key.low ^
        (key.high * 33U) ^ (unsigned(key.shader) * 17U) ^ key.flags);
}
struct XboxTextureNodeKey {
    const Interpreter* owner;
    unsigned epoch;
    TextureCacheKey texture;
    bool operator==(const XboxTextureNodeKey& rhs) const {
        return owner == rhs.owner && epoch == rhs.epoch && texture == rhs.texture;
    }
};
static __declspec(thread) Shipyard::Fast3D::MaterialMemo<XboxTextureNodeKey, TextureCacheNode*, 1024> sXboxTextureNodes;
// Same result as map.find(key): the epoch proves the remembered node is still
// in the map, and its LRU entry holds its map iterator (the map never rehashes
// while it holds nodes).
static __forceinline TextureCacheMap::iterator XboxFindTextureNode(Interpreter& gfx, const TextureCacheKey& key) {
    const XboxTextureNodeKey nodeKey = { &gfx, sXboxTextureEpoch, key };
    Shipyard::Fast3D::MaterialMemo<XboxTextureNodeKey, TextureCacheNode*, 1024>::Entry& entry =
        sXboxTextureNodes.Slot(Shipyard::Fast3D::HashMaterialKey(unsigned(reinterpret_cast<uintptr_t>(key.texture_addr)) ^
            key.size_bytes ^ (unsigned(key.fmt) << 12) ^ (unsigned(key.siz) << 16) ^ (unsigned(key.palette_index) << 20)));
    if (entry.valid && entry.key == nodeKey)
        return entry.value->second.lru_location->it;
    TextureCacheMap::iterator it = gfx.mTextureCache.map.find(key);
    if (it != gfx.mTextureCache.map.end()) {
        entry.valid = false;
        entry.key = nodeKey;
        entry.value = &*it;
        entry.valid = true;
    }
    return it;
}
// SHIPYARD MATERIAL REPLAY END

'''

LOOKUP = '''    const XboxModeKey xboxModeKey = XboxCaptureMode(*this);
    Shipyard::Fast3D::MaterialMemo<XboxModeKey, XboxModeValue, 256>::Entry& xboxMode =
        sXboxModes.Slot(XboxHashMode(xboxModeKey));
    if (!xboxMode.valid || !(xboxMode.key == xboxModeKey)) {
        // SHIPYARD MODE MISS BEGIN
%(body)s        // SHIPYARD MODE MISS END
        xboxMode.valid = false;
        xboxMode.key = xboxModeKey;
        xboxMode.value.comb = comb;
%(store)s        xboxMode.valid = true;
    }
    ColorCombiner* comb = xboxMode.value.comb;
%(load)s'''

DECLARATION = re.compile(r'^    (?:const )?[A-Za-z_][\w:]*[&*]?\s+[&*]?(\w+)\s*(?:=|;|\[)', re.M)


def _once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Material replay anchor drift: ' + old.strip()[:80])
    return text.replace(old, new)


def _bump(text, pattern, epoch):
    """Advance `epoch` before every statement line matching `pattern`; reject mid-line uses."""
    statement = re.compile(r'^([ \t]*)((?:\w+ = )?[\w>-]*' + pattern + ')', re.M)
    text, count = statement.subn(lambda m: m[1] + '++Fast::' + epoch + ';\n' + m[1] + m[2], text)
    if not count or count != len(re.findall(pattern, text)):
        raise ValueError('Material replay invalidation inventory changed: ' + pattern)
    return text


def install(text, *, function, sha256, values, hoist=()):
    """Memoize the mode decode in `function` and texture-node lookups.

    The decode runs from `uint64_t cc_id = mRdp->combine_mode;` through
    LookupOrCreateColorCombiner; `sha256` pins that block, whose complete input
    set is XboxCaptureMode's key. `values` are the bool locals the rest of the
    function reads (cc_id and comb are implicit); `hoist` lines, such as
    observer marks, move ahead of the lookup so they also run on a hit.
    """
    begin = text.index(function)
    finish = text.index('\n}', begin)
    method = text[begin:finish]
    first = method.index(MODE_START)
    last = method.index(MODE_END, first) + len(MODE_END)
    block = method[first:last]
    if hashlib.sha256(block.encode()).hexdigest() != sha256:
        raise ValueError('Re-audit the material mode decode and its complete key')
    body = block[len(MODE_START):]
    for line in hoist:
        body = _once(body, line + '\n', '')
    rest = method[last:]
    for name in DECLARATION.findall(body):
        # Member accesses (out.key, comb->key) are not uses of the local.
        if name not in values and name != 'comb' and re.search(r'(?<!\.)(?<!->)\b' + name + r'\b', rest):
            raise ValueError('Material mode local escapes the memo: ' + name)
    lookup = LOOKUP % dict(
        body=body,
        store=''.join('        xboxMode.value.%s = %s;\n' % (name, name) for name in values),
        load=''.join('    bool %s = xboxMode.value.%s;\n' % (name, name) for name in values))
    method = method[:first] + MODE_START + ''.join(line + '\n' for line in hoist) + lookup + rest
    text = text[:begin] + method + text[finish:]

    signature = 'bool Interpreter::TextureCacheLookup('
    begin = text.index(signature)
    finish = text.index('\n}', begin)
    lookup = _once(text[begin:finish], 'mTextureCache.map.find(key)', 'XboxFindTextureNode(*this, key)')
    text = text[:begin] + lookup + text[finish:]

    anchor = 'Interpreter::Interpreter()'
    if text.count(anchor) != 1:
        raise ValueError('Material replay anchor drift: ' + anchor)
    helpers = text.index(anchor)
    text = _bump(text, r'mColorCombinerPool\.clear\(\);', 'sXboxCombinerEpoch')
    text = _bump(text, r'mTextureCache\.map\.(?:erase\(|clear\(\);)', 'sXboxTextureEpoch')
    # A backend (re)initialization starts both memos over.
    text, count = re.subn(r'^([ \t]*)(.*\bmRapi->Init\(\))',
                          r'\1++Fast::sXboxCombinerEpoch;\n\1++Fast::sXboxTextureEpoch;\n\1\2', text, flags=re.M)
    if count != 1:
        raise ValueError('Material replay invalidation inventory changed: mRapi->Init()')
    for use in ('++Fast::sXbox', 'XboxFindTextureNode(*this', 'XboxCaptureMode(*this)'):
        if text.index(use) < helpers:
            raise ValueError('Material replay use precedes its helpers: ' + use)
    text = text[:helpers] + HELPERS % dict(values=''.join('    bool %s;\n' % name for name in values)) + text[helpers:]
    text = _once(text, 'Interpreter::~Interpreter() {', '''Interpreter::~Interpreter() {
    ++sXboxCombinerEpoch;
    ++sXboxTextureEpoch;
    sXboxModes.Release();
    sXboxTextureNodes.Release();''')
    return '#include "fast3d/material_memo.h"\n' + text
