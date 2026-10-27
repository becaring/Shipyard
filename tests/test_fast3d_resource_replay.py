"""Run-scoped managers and batch texture replays, executed against the unprojected handler."""
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
sys.path.insert(0, str(ROOT / 'tests'))
from xbox360_platform import fast3d_resource_replay as replay
from test_renderer_replay import compile_run

STORE = '            gfx->GfxDpSetTextureImage(C0(21, 3), C0(19, 2), C0(0, 12) + 1, fileName, texFlags, rawTexMetadata, tex);'
CONTEXT = 'Ship::Context::GetRawInstance()'

# Host stand-ins for the LUS types; the handler is shaped like Majora's.
SOURCE = r'''#include <cassert>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>
static unsigned gManagerCopies, gSlow;
struct Texture {
    char* ImageData; unsigned Flags, Width; bool dirty;
    bool IsDirty() const { return dirty; }
};
struct RawTexMetadata { unsigned width; std::shared_ptr<Texture> resource; RawTexMetadata() : width(0) {} };
namespace Ship {
struct ArchiveManager {
    unsigned epoch; std::map<uint64_t, std::string> names;
    unsigned GetXboxHashEpoch() const { return epoch; }
    const char* HashToCString(uint64_t hash) const {
        std::map<uint64_t, std::string>::const_iterator it = names.find(hash);
        return it == names.end() ? NULL : it->second.c_str();
    }
};
struct ResourceManager {
    unsigned generation; std::shared_ptr<ArchiveManager> archives; std::map<std::string, std::shared_ptr<Texture> > textures;
    unsigned GetXboxLookupGeneration() const { return generation; }
    std::shared_ptr<ArchiveManager> GetArchiveManager() { ++gManagerCopies; return archives; }
    std::shared_ptr<Texture> Load(const char* name) { return textures[name]; }
};
struct Context {
    std::shared_ptr<ResourceManager> resources;
    static Context* GetRawInstance();
    std::shared_ptr<ResourceManager> GetResourceManager() const { ++gManagerCopies; return resources; }
};
static Context sContext;
Context* Context::GetRawInstance() { return &sContext; }
}
union F3DGfx { struct { uintptr_t w0, w1; } words; };
#define C0(pos, width) ((cmd->words.w0 >> (pos)) & ((1U << width) - 1))
struct Bind { unsigned fmt, size, width, flags, metaWidth; const void* addr; Texture* resource; };
struct Interpreter {
    std::vector<Bind> binds;
    ~Interpreter();
    void GfxDpSetTextureImage(unsigned fmt, unsigned size, unsigned width, const char*, unsigned flags,
                              const RawTexMetadata& meta, const void* addr) {
        Bind bind = { fmt, size, width, flags, meta.width, addr, meta.resource.get() };
        binds.push_back(bind);
    }
};
struct XboxDispatchOwner {
    XboxDispatchOwner(void*, Interpreter*) {}
    static Interpreter* TemporaryBorrow(Interpreter* p) { return p; }
};
static Interpreter* mInstance;
static void* sXboxDispatchOwner;
static Interpreter* XboxDispatchInterpreter() {
    return XboxDispatchOwner::TemporaryBorrow(mInstance);
}

Interpreter::~Interpreter() {
}

void Interpreter::TextureCacheClear() {
}

bool gfx_set_timg_otr_hash_handler_custom(F3DGfx** cmd0) {
    (*cmd0)++;
    uint64_t hash = ((uint64_t)(*cmd0)->words.w0 << 32) + (uint64_t)(*cmd0)->words.w1;
    const char* fileName = XboxRunArchiveManager(XboxRunResourceManager())->HashToCString(hash);
    uint32_t texFlags = 0;
    RawTexMetadata rawTexMetadata;
    if (fileName == nullptr) {
        (*cmd0)++;
        return false;
    }
    ++gSlow;
    std::shared_ptr<Texture> texture = XboxRunResourceManager()->Load(fileName);
    if (texture != nullptr) {
        texFlags = texture->Flags;
        rawTexMetadata.width = texture->Width;
        rawTexMetadata.resource = texture;
        char* tex = texture->ImageData;
        if (tex != nullptr) {
            (*cmd0)--;
            (*cmd0)->words.w1 = (uintptr_t)tex;
            (*cmd0)++;
        }
        (*cmd0)--;
        F3DGfx* cmd = (*cmd0);
        if (tex != NULL) {
            Interpreter* gfx = XboxDispatchInterpreter();
            gfx->GfxDpSetTextureImage(C0(21, 3), C0(19, 2), C0(0, 12) + 1, fileName, texFlags, rawTexMetadata, tex);
        }
    }
    (*cmd0)++;
    return false;
}

static void Run(std::vector<F3DGfx>& commands) {
    XboxDispatchOwner dispatchOwner(sXboxDispatchOwner, mInstance);
    // The unknown hash skips one extra word, as upstream does; padding keeps it in bounds.
    for (F3DGfx* cmd = &commands[0]; cmd < &commands[0] + 12; ++cmd)
        gfx_set_timg_otr_hash_handler_custom(&cmd);
}

static std::vector<F3DGfx> Commands() {
    std::vector<F3DGfx> commands;
    // Repeated and pixel-less textures, then an unknown hash and padding.
    const uint64_t hashes[] = { 11, 22, 11, 44, 22, 33, 0 };
    for (unsigned i = 0; i < 7; ++i) {
        F3DGfx first, second;
        first.words.w0 = (uintptr_t(i & 7) << 21) | (uintptr_t(i & 3) << 19) | (31 + i);
        first.words.w1 = 0x1000 + i;
        second.words.w0 = uintptr_t(hashes[i] >> 32);
        second.words.w1 = uintptr_t(hashes[i]);
        commands.push_back(first);
        commands.push_back(second);
    }
    return commands;
}

// Presents `replays` copies of one batch; returns every bind and patched word.
static std::vector<uintptr_t> Batch(unsigned replays, int mutate) {
    std::vector<uintptr_t> seen;
    std::vector<F3DGfx> commands = Commands();
    Shipyard_TextureReplayBegin(replays);
    for (unsigned r = 0; r < replays; ++r) {
        if (r == 1 && mutate == 1) ++Ship::sContext.resources->generation;
        if (r == 1 && mutate == 2) ++Ship::sContext.resources->archives->epoch;
        if (r == 1 && mutate == 3) Ship::sContext.resources->textures["tex/a"]->dirty = true;
        if (r == 1 && mutate == 4) mInstance->TextureCacheClear();
        mInstance->binds.clear();
        Run(commands);
        for (size_t i = 0; i < mInstance->binds.size(); ++i) {
            const Bind& b = mInstance->binds[i];
            seen.push_back(b.fmt); seen.push_back(b.size); seen.push_back(b.width); seen.push_back(b.flags);
            seen.push_back(b.metaWidth); seen.push_back((uintptr_t)b.addr); seen.push_back((uintptr_t)b.resource);
        }
        for (size_t i = 0; i < commands.size(); ++i) seen.push_back(commands[i].words.w1);
    }
    Shipyard_TextureReplayEnd();
    if (mutate == 3) Ship::sContext.resources->textures["tex/a"]->dirty = false;
    return seen;
}

int main() {
    static char a[4], b[4];
    Interpreter interpreter;
    mInstance = &interpreter;
    std::shared_ptr<Ship::ResourceManager> manager(new Ship::ResourceManager);
    manager->generation = 1;
    manager->archives.reset(new Ship::ArchiveManager);
    manager->archives->epoch = 1;
    manager->archives->names[11] = "tex/a";
    manager->archives->names[22] = "tex/b";
    manager->archives->names[44] = "tex/empty";
    Texture ta = { a, 5, 32, false }, tb = { b, 6, 64, false }, te = { NULL, 7, 8, false };
    manager->textures["tex/a"] = std::shared_ptr<Texture>(&ta, [](Texture*) {});
    manager->textures["tex/b"] = std::shared_ptr<Texture>(&tb, [](Texture*) {});
    manager->textures["tex/empty"] = std::shared_ptr<Texture>(&te, [](Texture*) {});
    Ship::sContext.resources = manager;

    // Reference: replays disabled, every command takes the unprojected path.
    gShipyardTextureReplayControl[0] = 0;
    for (int mutate = 0; mutate <= 4; ++mutate) {
        gShipyardTextureReplayControl[0] = 0;
        gSlow = 0;
        const std::vector<uintptr_t> reference = Batch(3, mutate);
        const unsigned referenceSlow = gSlow;
        assert(referenceSlow == 15);
        gShipyardTextureReplayControl[0] = 1;
        gSlow = 0;
        gManagerCopies = 0;
        const std::vector<uintptr_t> candidate = Batch(3, mutate);
        assert(candidate == reference);
        // Replay 0 resolves each known hash once; later replays reuse unless the
        // certificate moved (1 generation, 2 epoch, 4 clear) or the texture is dirty (3).
        const unsigned expected = mutate == 0 ? 5 : mutate == 3 ? 9 : 7;
        if (gSlow != expected) { std::printf("mutate %d slow %u\n", mutate, gSlow); return 1; }
        // Two copies per Run scope; texture commands borrow its managers.
        assert(gManagerCopies == 6);
    }
    gShipyardTextureReplayControl[0] = 1;
    gSlow = 0;
    Batch(1, 0);
    assert(gSlow == 5 && !gShipyardTextureReplayControl[1]);
    // Outside a Run the borrow falls back to the authoritative acquisition.
    gManagerCopies = 0;
    assert(XboxRunArchiveManager(XboxRunResourceManager()) == manager->archives.get() && gManagerCopies == 2);
    std::printf("texture replays match the unprojected handler across 5 batch mutations\n");
}
'''


def project(text):
    text = replay.run_scope(text, context=CONTEXT, run='    XboxDispatchOwner dispatchOwner(sXboxDispatchOwner, mInstance);\n')
    return replay.texture_replays(
        text, store=STORE, clears=('void Interpreter::TextureCacheClear() {',),
        generation='XboxRunResourceManager()->GetXboxLookupGeneration()',
        epoch='XboxRunArchiveManager(XboxRunResourceManager())->GetXboxHashEpoch()')


class ResourceReplayTests(unittest.TestCase):
    def test_replays_match_unprojected_handler(self):
        source = SOURCE.replace('    void GfxDpSetTextureImage(', '    void TextureCacheClear();\n    void GfxDpSetTextureImage(')
        text = project(source)
        self.assertEqual(text.count('XboxClearTextureReplays();'), 1)
        self.assertLess(text.index('XboxRunResourceScope xboxRunResources;'), text.index('    for (F3DGfx* cmd'))
        self.assertLess(text.index('sXboxTextureReplays->Store'), text.index(STORE))
        compile_run(self, text)

    def test_changed_handler_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'Texture hash handler drift'):
            project(SOURCE.replace(STORE, STORE.replace('tex);', 'tex );')))
        with self.assertRaisesRegex(ValueError, 'Resource replay anchor drift'):
            project(SOURCE.replace('TemporaryBorrow(mInstance);\n}', 'TemporaryBorrow(mInstance); }'))


if __name__ == '__main__':
    unittest.main()
