"""Run-scoped resource managers and batch texture replays for OTR commands.

run_scope: every replayed resource command re-acquires the Context-owned
resource and archive managers through shared_ptr copies (an atomic reference
count pair each). One scope per Run owns both managers; commands borrow raw
pointers and code outside a Run keeps the normal acquisition.

texture_replays: an interpolation batch presents one command list several
times. The first resolution of each texture hash owns its resource, path and
metadata (fast3d/texture_replay.h); later replays reuse it while the lookup
generation and archive epoch are unchanged and the resource stays clean. A miss
runs the unchanged handler; a hit makes the same pointer patch and image bind.
"""

DISPATCH = '    return XboxDispatchOwner::TemporaryBorrow(mInstance);\n}'
RUN = '    XboxDispatchOwner dispatchOwner(sXboxDispatchOwner, mInstance);\n'
HANDLER = 'bool gfx_set_timg_otr_hash_handler_custom(F3DGfx** cmd0) {'
SLOW = 'static __declspec(noinline) bool XboxResolveTextureHash(F3DGfx** cmd0) {'

RUN_SCOPE = r'''

// SHIPYARD RUN RESOURCE SCOPE BEGIN
static __declspec(thread) Ship::ResourceManager* sXboxRunResourceManager = 0;
static __declspec(thread) Ship::ArchiveManager* sXboxRunArchiveManager = 0;
struct XboxRunResourceScope {
    std::shared_ptr<Ship::ResourceManager> resources;
    std::shared_ptr<Ship::ArchiveManager> archives;
    Ship::ResourceManager* previousResources;
    Ship::ArchiveManager* previousArchives;
    XboxRunResourceScope()
        : previousResources(sXboxRunResourceManager), previousArchives(sXboxRunArchiveManager) {
        const auto context = CONTEXT;
        if (context) resources = context->GetResourceManager();
        if (resources) archives = resources->GetArchiveManager();
        sXboxRunResourceManager = resources.get();
        sXboxRunArchiveManager = archives.get();
    }
    ~XboxRunResourceScope() {
        sXboxRunResourceManager = previousResources;
        sXboxRunArchiveManager = previousArchives;
    }
  private:
    XboxRunResourceScope(const XboxRunResourceScope&);
    XboxRunResourceScope& operator=(const XboxRunResourceScope&);
};
static __declspec(noinline) Ship::ResourceManager* XboxResourceManagerFallback() {
    return CONTEXT->GetResourceManager().get();
}
static __declspec(noinline) Ship::ArchiveManager* XboxArchiveManagerFallback(Ship::ResourceManager* manager) {
    return manager->GetArchiveManager().get();
}
static __forceinline Ship::ResourceManager* XboxRunResourceManager() {
    Ship::ResourceManager* const manager = sXboxRunResourceManager;
    return manager ? manager : XboxResourceManagerFallback();
}
static __forceinline Ship::ArchiveManager* XboxRunArchiveManager(Ship::ResourceManager* manager) {
    Ship::ArchiveManager* const archives = sXboxRunArchiveManager;
    return archives && manager == sXboxRunResourceManager ? archives : XboxArchiveManagerFallback(manager);
}
// SHIPYARD RUN RESOURCE SCOPE END'''

REPLAYS = r'''// SHIPYARD TEXTURE REPLAYS BEGIN
// Live A/B control sampled at each batch start: [0] enables reuse, [1] reports it.
extern "C" volatile unsigned gShipyardTextureReplayControl[2] = { 1, 0 };
typedef Shipyard::Fast3D::TextureReplayCache<RawTexMetadata> XboxTextureReplays;
static __declspec(thread) XboxTextureReplays* sXboxTextureReplays = NULL;
// The render thread brackets each interpolation batch of one command list.
extern "C" void Shipyard_TextureReplayBegin(unsigned replays) {
    if (!gShipyardTextureReplayControl[0]) replays = 0;
    gShipyardTextureReplayControl[1] = replays > 1 ? 1 : 0;
    if (!sXboxTextureReplays && replays > 1) sXboxTextureReplays = new XboxTextureReplays;
    if (sXboxTextureReplays) sXboxTextureReplays->Begin(replays);
}
extern "C" void Shipyard_TextureReplayEnd(void) {
    if (sXboxTextureReplays) sXboxTextureReplays->End();
}
static void XboxClearTextureReplays() {
    if (sXboxTextureReplays) sXboxTextureReplays->Clear();
}
// SHIPYARD TEXTURE REPLAYS END

'''

WRAPPER = r'''
bool gfx_set_timg_otr_hash_handler_custom(F3DGfx** cmd0) {
    F3DGfx* cmd = *cmd0;
    const uint64_t hash = (static_cast<uint64_t>(cmd[1].words.w0) << 32) + cmd[1].words.w1;
    Interpreter* gfx = XboxDispatchInterpreter();
    const XboxTextureReplays::Entry* hit = NULL;
    if (sXboxTextureReplays && sXboxTextureReplays->active) {
        sXboxTextureReplays->Certify(GENERATION, EPOCH);
        hit = sXboxTextureReplays->Lookup(gfx, hash);
    }
    if (!hit) return XboxResolveTextureHash(cmd0);
    // The command's format, size and width can differ under one shared hash.
    char* pixels = reinterpret_cast<char*>(hit->metadata.resource->ImageData);
    if (pixels) {
        cmd->words.w1 = reinterpret_cast<uintptr_t>(pixels);
        gfx->GfxDpSetTextureImage(C0(21, 3), C0(19, 2), C0(0, 12) + 1,
                                 hit->path.c_str(), hit->flags, hit->metadata, pixels);
    }
    ++(*cmd0);
    return false;
}'''


def _once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Resource replay anchor drift: ' + old[:90])
    return text.replace(old, new)


def run_scope(text, *, context, after=DISPATCH, run=RUN):
    """Install the scope inside Run; consumers then route acquisitions through
    XboxRunResourceManager() and XboxRunArchiveManager(manager)."""
    text = _once(text, after, after + RUN_SCOPE.replace('CONTEXT', context))
    return _once(text, run, run + '    XboxRunResourceScope xboxRunResources;\n')


def texture_replays(text, *, store, generation, epoch, clears, destructor='Interpreter::~Interpreter() {'):
    """store: the handler's image-bind line, reached only with a resolved
    texture and its owner `gfx`, `hash`, `fileName`, `texFlags`, `rawTexMetadata`
    in scope. clears: lines after which texture resolution may change."""
    text = '#include "fast3d/texture_replay.h"\n' + _once(text, destructor, REPLAYS + destructor)
    text = _once(text, destructor, destructor + '\n    delete sXboxTextureReplays;\n    sXboxTextureReplays = NULL;')
    for anchor in clears:
        indent = anchor[:len(anchor) - len(anchor.lstrip())] + ('    ' if anchor.endswith('{') else '')
        text = _once(text, anchor, anchor + '\n' + indent + 'XboxClearTextureReplays();')
    start = text.index(HANDLER)
    end = text.index('\n}', start) + 2
    if text.count(HANDLER) != 1 or text[start:end].count(store) != 1:
        raise ValueError('Texture hash handler drift')
    indent = store[:len(store) - len(store.lstrip())]
    slow = text[start:end].replace(HANDLER, SLOW).replace(
        store, indent + 'if (sXboxTextureReplays)\n' + indent +
        '    sXboxTextureReplays->Store(gfx, hash, fileName, texFlags, rawTexMetadata);\n' + store)
    wrapper = WRAPPER.replace('GENERATION', generation).replace('EPOCH', epoch)
    return text[:start] + slow + '\n' + wrapper + text[end:]
