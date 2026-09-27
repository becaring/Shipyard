"""Projection and native syntax contract for sampled texture hash timing."""

import sys
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
from xbox360_platform.resource_lookup import (texture_hash_phase_profile,
                                               texture_hash_cache_handler)


HANDLER = '''bool gfx_set_timg_otr_hash_handler_custom(F3DGfx** cmd0) {
    const auto resourceManager = Ship::Context::GetInstance()->GetResourceManager();
    const auto archiveManager = resourceManager->GetArchiveManager();
    Interpreter* gfx = XboxDispatchInterpreter();
    const unsigned lookupGeneration = resourceManager->GetXboxLookupGeneration();
    const unsigned archiveEpoch = archiveManager->GetXboxHashEpoch();
    std::shared_ptr<Fast::Texture> texture;
    std::shared_ptr<const std::string> ownedHashPath;
    const char* fileName = nullptr;
    const bool hashHit = gfx->mXboxTextureHashCache.LookupArchiveHash(
        hash, lookupGeneration, archiveEpoch, resourceManager, archiveManager,
        texture, ownedHashPath);
    if (hashHit &&
        resourceManager->GetXboxLookupGeneration() == lookupGeneration &&
        archiveManager->GetXboxHashEpoch() == archiveEpoch) {
        fileName = ownedHashPath->c_str();
    } else {
        fileName = archiveManager->HashToCString(hash);
        if (fileName == nullptr) {
            (*cmd0)++;
            return false;
        }
        texture = std::static_pointer_cast<Fast::Texture>(
            resourceManager->LoadResourceProcess(fileName));
        if (texture && !texture->IsDirty() &&
            resourceManager->GetXboxLookupGeneration() == lookupGeneration &&
            archiveManager->GetXboxHashEpoch() == archiveEpoch)
            gfx->mXboxTextureHashCache.StoreArchiveHash(
                hash, fileName, lookupGeneration, archiveEpoch, texture,
                resourceManager, archiveManager);
    }
    uint32_t texFlags = 0;
    RawTexMetadata rawTexMetadata;
    if (texture != nullptr) {
        texFlags = texture->Flags;
        rawTexMetadata.resource = texture;
        gfx->GfxDpSetTextureImage(texFlags, rawTexMetadata);
    }
    (*cmd0)++;
    return false;
}
bool next_handler() { return true; }
'''


LOWERED_SOURCE = '''bool gfx_set_timg_otr_hash_handler_custom(F3DGfx** cmd0) {
    const auto resourceManager = Ship::Context::GetInstance()->GetResourceManager();
    const char* fileName = resourceManager->GetArchiveManager()->HashToCString(hash);
    uint32_t texFlags = 0;
    RawTexMetadata rawTexMetadata;

    if (fileName == nullptr) {
        (*cmd0)++;
        return false;
    }

    std::shared_ptr<Fast::Texture> texture =
        std::static_pointer_cast<Fast::Texture>(resourceManager->LoadResourceProcess(
            fileName));
    if (texture != nullptr) {
        // OTRTODO: We have disabled caching for now to fix a texture corruption issue with HD texture
        // support. In doing so, there is a potential performance hit since we are not caching lookups. We
        // need to do proper profiling to see whether or not it is worth it to keep the caching system.

        if (texture->ImageData) {
            Interpreter* gfx = XboxDispatchInterpreter();
            gfx->GfxDpSetTextureImage(texFlags, rawTexMetadata);
        }
    }
    (*cmd0)++;
    return false;
}
bool gfx_set_timg_handler_rdp(F3DGfx** cmd0) {
    if (true) {
            std::shared_ptr<Fast::Texture> tex = std::static_pointer_cast<Fast::Texture>(
                Ship::Context::GetInstance()->GetResourceManager()->LoadResourceProcess(imgData));
    }
    return false;
}
bool next_handler() { return true; }
'''


PREFIX = '''#include <stdint.h>
#include <memory>
#include <string>
struct F3DGfx { int value; };
namespace Fast { struct Texture { unsigned Flags; bool IsDirty() const; }; }
struct RawTexMetadata { std::shared_ptr<Fast::Texture> resource; };
namespace ModernFrameProfile {
enum TextureHashPhase { TextureHashMapLookup, TextureHashOwningCache,
                        TextureHashLoader, TextureHashFinal };
struct TextureHashSample { bool sampled; };
void BeginTextureHash(TextureHashSample&, TextureHashPhase);
void AdvanceTextureHash(TextureHashSample&, TextureHashPhase);
void EndTextureHash(TextureHashSample&);
}
struct ArchiveManager {
    const char* HashToCString(uint64_t) const;
    unsigned GetXboxHashEpoch() const;
};
struct ResourceManager {
    std::shared_ptr<ArchiveManager> GetArchiveManager();
    unsigned GetXboxLookupGeneration() const;
    std::shared_ptr<Fast::Texture> LoadResourceProcess(const char*);
};
namespace Ship { struct Context {
    static Context* GetInstance();
    std::shared_ptr<ResourceManager> GetResourceManager();
}; }
struct Cache {
    bool LookupArchiveHash(uint64_t, unsigned, unsigned,
                           const std::shared_ptr<ResourceManager>&,
                           const std::shared_ptr<ArchiveManager>&,
                           std::shared_ptr<Fast::Texture>&,
                           std::shared_ptr<const std::string>&);
    void StoreArchiveHash(uint64_t, const char*, unsigned, unsigned,
                          const std::shared_ptr<Fast::Texture>&,
                          const std::shared_ptr<ResourceManager>&,
                          const std::shared_ptr<ArchiveManager>&);
};
struct Interpreter {
    Cache mXboxTextureHashCache;
    void GfxDpSetTextureImage(unsigned, const RawTexMetadata&);
};
Interpreter* XboxDispatchInterpreter();
uint64_t hash = 0;
'''


class TextureHashPhaseProfileTests(unittest.TestCase):
    def test_complete_cache_projection_accepts_lowered_metadata_local(self):
        projected = texture_hash_cache_handler(LOWERED_SOURCE)
        handler = projected.split('bool gfx_set_timg_handler_rdp(', 1)[0]
        self.assertLess(handler.index('LookupArchiveHash('),
                        handler.index('HashToCString(hash)'))
        self.assertLess(handler.index('HashToCString(hash)'),
                        handler.index('RawTexMetadata rawTexMetadata;'))
        self.assertEqual(handler.count('BeginTextureHash('), 1)
        self.assertEqual(handler.count('EndTextureHash('), 2)
        self.assertNotIn('RawTexMetadata rawTexMetadata = {};', handler)
        self.assertIn('StoreArchiveHash(', handler)
        with self.assertRaises(ValueError):
            texture_hash_cache_handler(LOWERED_SOURCE.replace(
                'RawTexMetadata rawTexMetadata;', 'RawTexMetadata rawTexMetadata = {};'))

    def test_missing_map_hit_and_fallback_have_one_end(self):
        projected = texture_hash_phase_profile(HANDLER)
        handler = projected.split('bool next_handler()', 1)[0]
        self.assertEqual(handler.count('BeginTextureHash('), 1)
        self.assertEqual(handler.count('EndTextureHash(xboxTextureHashSample)'), 2)
        self.assertEqual(handler.count('TextureHashOwningCache'), 1)
        self.assertEqual(handler.count('TextureHashLoader'), 1)
        self.assertEqual(handler.count('TextureHashFinal'), 3)
        self.assertEqual(handler.count('TextureHashMapLookup'), 1)
        self.assertLess(handler.index('BeginTextureHash('),
                        handler.index('LookupArchiveHash('))
        self.assertLess(handler.index('HashToCString(hash)'),
                        handler.index('TextureHashFinal', handler.index('HashToCString(hash)')))
        self.assertLess(handler.index('TextureHashOwningCache'),
                        handler.index('LookupArchiveHash('))
        self.assertNotIn('AdvanceTextureHash(', handler.split('LookupArchiveHash(', 1)[0])
        self.assertLess(handler.index('TextureHashMapLookup'),
                        handler.index('HashToCString(hash)'))
        self.assertLess(handler.index('TextureHashLoader'),
                        handler.index('LoadResourceProcess(fileName)'))
        cache_hit = handler.split('if (hashHit &&', 1)[1].split('    } else {', 1)[0]
        miss = handler.split('    } else {', 1)[1].split('\n    }', 1)[0]
        missing_map = miss.split('if (fileName == nullptr) {', 1)[1].split('\n        }', 1)[0]
        loader = miss.split('if (fileName == nullptr) {', 1)[1].split('\n        }', 1)[1]
        loader = loader.split('LoadResourceProcess(fileName)', 1)[0]
        final = handler.split('    } else {', 1)[1].split('\n    }', 1)[1]
        self.assertEqual(missing_map.count('EndTextureHash('), 1)
        self.assertEqual(cache_hit.count('EndTextureHash('), 0)
        self.assertEqual(cache_hit.count('TextureHashFinal'), 1)
        self.assertEqual(loader.count('EndTextureHash('), 0)
        self.assertEqual(loader.count('TextureHashLoader'), 1)
        self.assertEqual(final.count('EndTextureHash('), 1)
        self.assertEqual(projected.split('bool next_handler()', 1)[1],
                         HANDLER.split('bool next_handler()', 1)[1])

    def test_projection_fails_closed(self):
        with self.assertRaises(ValueError):
            texture_hash_phase_profile(texture_hash_phase_profile(HANDLER))
        with self.assertRaises(ValueError):
            texture_hash_phase_profile(HANDLER.replace('HashToCString(hash)',
                                                       'ChangedHashLookup(hash)'))
        with self.assertRaises(ValueError):
            texture_hash_phase_profile(HANDLER.replace('resourceManager, archiveManager);',
                                                       'resourceManager, otherArchive);'))

    def test_projected_handler_compiles_for_xenon(self):
        xdk = Path('C:/Program Files (x86)/Microsoft Xbox 360 SDK')
        with tempfile.TemporaryDirectory(prefix='shipyard-texture-phase-') as folder:
            source = Path(folder) / 'texture_phase.cpp'
            output = Path(folder) / 'texture_phase.obj'
            source.write_text(PREFIX + texture_hash_phase_profile(HANDLER))
            result = subprocess.run([
                str(xdk / 'bin/win32/cl.exe'), '/nologo', '/c', '/O2', '/EHsc',
                '/D_XBOX', '/D_XBOX_VER=200', '/I' + str(xdk / 'include/xbox'),
                '/Fo' + str(output), str(source),
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(output.read_bytes()[:2], b'\xf2\x01')


if __name__ == '__main__':
    unittest.main()
