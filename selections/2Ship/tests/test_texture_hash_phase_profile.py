"""Projection and native syntax contract for sampled texture hash timing."""

import sys
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
from xbox360_platform.resource_lookup import (texture_hash_phase_profile, texture_hash_borrowed_handler,
                                               texture_hash_epoch_handler)


HANDLER = '''bool gfx_set_timg_otr_hash_handler_custom(F3DGfx** cmd0) {
    const auto resourceManager = Ship::Context::GetInstance()->GetResourceManager();
    const char* fileName = resourceManager->GetArchiveManager()->HashToCString(hash);
    uint32_t texFlags = 0;
    RawTexMetadata rawTexMetadata;
    if (fileName == nullptr) {
        (*cmd0)++;
        return false;
    }
    Interpreter* gfx = XboxDispatchInterpreter();
    const unsigned lookupGeneration = resourceManager->GetXboxLookupGeneration();
    std::shared_ptr<Fast::Texture> texture =
        gfx->mXboxTextureHashCache.Lookup(hash, fileName, lookupGeneration);
    if (!texture) {
        texture = std::static_pointer_cast<Fast::Texture>(
            resourceManager->LoadResourceProcess(fileName));
        if (texture && !texture->IsDirty() &&
            resourceManager->GetXboxLookupGeneration() == lookupGeneration)
            gfx->mXboxTextureHashCache.Store(hash, fileName, lookupGeneration, texture);
    }
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
void BeginTextureHash(TextureHashSample&);
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
    ResourceManager* GetResourceManager();
}; }
struct Cache {
    struct Entry { std::string path; std::shared_ptr<Fast::Texture> texture; };
    std::shared_ptr<Fast::Texture> Lookup(uint64_t, const char*, unsigned);
    const std::shared_ptr<Fast::Texture>* LookupBorrowed(uint64_t, const char*, unsigned);
    const Entry* LookupArchive(uint64_t, const void*, unsigned, unsigned);
    const Entry* LookupAndCertifyArchive(uint64_t, const char*, unsigned, const void*, unsigned);
    void Store(uint64_t, const char*, unsigned, std::shared_ptr<Fast::Texture>);
    void StoreArchive(uint64_t, const char*, unsigned, const void*, unsigned,
                      std::shared_ptr<Fast::Texture>);
};
struct Interpreter {
    Cache mXboxTextureHashCache;
    void GfxDpSetTextureImage(unsigned, const RawTexMetadata&);
};
Interpreter* XboxDispatchInterpreter();
uint64_t hash = 0;
'''


class TextureHashPhaseProfileTests(unittest.TestCase):
    def test_archive_epoch_route_preserves_authoritative_fallback(self):
        projected = texture_hash_epoch_handler(texture_hash_borrowed_handler(
            texture_hash_phase_profile(HANDLER)))
        self.assertLess(projected.index('mXboxTextureHashCache.LookupArchive('),
                        projected.index('HashToCString(hash)'))
        self.assertIn('archiveHit ? archiveHit->path.c_str() : archiveManager->HashToCString(hash)', projected)
        self.assertIn('LookupAndCertifyArchive(\n            hash, fileName, lookupGeneration,', projected)
        self.assertIn('if (fileName == nullptr)', projected)
        self.assertIn('resourceManager->LoadResourceProcess(fileName)', projected)
        self.assertIn('archiveManager->GetXboxHashEpoch() == hashEpoch', projected)
        self.assertIn('mXboxTextureHashCache.StoreArchive(\n                hash, fileName, lookupGeneration,', projected)
        self.assertIn('rawTexMetadata.resource = texture;', projected)

    def test_borrowed_hit_keeps_owner_until_metadata_copy(self):
        projected = texture_hash_borrowed_handler(texture_hash_phase_profile(HANDLER))
        self.assertIn('LookupBorrowed(hash, fileName, lookupGeneration)', projected)
        self.assertIn('const std::shared_ptr<Fast::Texture>& texture =', projected)
        self.assertIn('cachedTexture ? *cachedTexture : loadedTexture', projected)
        self.assertIn('loadedTexture = std::static_pointer_cast<Fast::Texture>', projected)
        self.assertIn('rawTexMetadata.resource = texture;', projected)
        self.assertNotIn('mXboxTextureHashCache.Lookup(hash,', projected)

    def test_missing_map_hit_and_fallback_have_one_end(self):
        projected = texture_hash_phase_profile(HANDLER)
        handler = projected.split('bool next_handler()', 1)[0]
        self.assertEqual(handler.count('BeginTextureHash(xboxTextureHashSample)'), 1)
        self.assertEqual(handler.count('EndTextureHash(xboxTextureHashSample)'), 2)
        self.assertEqual(handler.count('TextureHashOwningCache'), 1)
        self.assertEqual(handler.count('TextureHashLoader'), 1)
        self.assertEqual(handler.count('TextureHashFinal'), 2)
        self.assertLess(handler.index('BeginTextureHash(xboxTextureHashSample)'),
                        handler.index('HashToCString(hash)'))
        self.assertLess(handler.index('HashToCString(hash)'),
                        handler.index('TextureHashFinal'))
        self.assertLess(handler.index('EndTextureHash(xboxTextureHashSample)'),
                        handler.index('return false;'))
        self.assertLess(handler.index('TextureHashOwningCache'),
                        handler.index('mXboxTextureHashCache.Lookup'))
        self.assertLess(handler.index('TextureHashLoader'),
                        handler.index('LoadResourceProcess(fileName)'))
        missing_map = handler.split('if (fileName == nullptr) {', 1)[1].split('\n    }', 1)[0]
        cache_and_loader = handler.split('if (fileName == nullptr) {', 1)[1].split('\n    }', 1)[1]
        loader = cache_and_loader.split('if (!texture) {', 1)[1].split('\n    }', 1)[0]
        cache_hit = cache_and_loader.split('if (!texture) {', 1)[0]
        final = cache_and_loader.split('if (!texture) {', 1)[1].split('\n    }', 1)[1]
        self.assertEqual(missing_map.count('EndTextureHash('), 1)
        self.assertEqual(missing_map.count('AdvanceTextureHash('), 0)
        self.assertEqual(cache_hit.count('EndTextureHash('), 0)
        self.assertEqual(cache_hit.count('TextureHashOwningCache'), 1)
        self.assertEqual(loader.count('EndTextureHash('), 0)
        self.assertEqual(loader.count('TextureHashLoader'), 1)
        self.assertEqual(final.count('EndTextureHash('), 1)
        self.assertEqual(final.count('TextureHashFinal'), 1)
        self.assertEqual(projected.split('bool next_handler()', 1)[1],
                         HANDLER.split('bool next_handler()', 1)[1])

    def test_projection_fails_closed(self):
        with self.assertRaises(ValueError):
            texture_hash_phase_profile(texture_hash_phase_profile(HANDLER))
        with self.assertRaises(ValueError):
            texture_hash_phase_profile(HANDLER.replace('HashToCString(hash)',
                                                       'ChangedHashLookup(hash)'))
        with self.assertRaises(ValueError):
            texture_hash_phase_profile(HANDLER.replace('mXboxTextureHashCache.Store(hash,',
                                                       'mXboxTextureHashCache.Store(otherHash,'))

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

    def test_borrowed_handler_compiles_for_xenon(self):
        xdk = Path('C:/Program Files (x86)/Microsoft Xbox 360 SDK')
        with tempfile.TemporaryDirectory(prefix='shipyard-texture-borrow-') as folder:
            source = Path(folder) / 'texture_borrow.cpp'
            output = Path(folder) / 'texture_borrow.obj'
            source.write_text(PREFIX + texture_hash_borrowed_handler(texture_hash_phase_profile(HANDLER)))
            result = subprocess.run([
                str(xdk / 'bin/win32/cl.exe'), '/nologo', '/c', '/O2', '/EHsc',
                '/D_XBOX', '/D_XBOX_VER=200', '/I' + str(xdk / 'include/xbox'),
                '/Fo' + str(output), str(source),
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(output.read_bytes()[:2], b'\xf2\x01')

    def test_archive_epoch_handler_compiles_for_xenon(self):
        xdk = Path('C:/Program Files (x86)/Microsoft Xbox 360 SDK')
        with tempfile.TemporaryDirectory(prefix='shipyard-texture-epoch-') as folder:
            source = Path(folder) / 'texture_epoch.cpp'
            output = Path(folder) / 'texture_epoch.obj'
            source.write_text(PREFIX + texture_hash_epoch_handler(texture_hash_borrowed_handler(
                texture_hash_phase_profile(HANDLER))))
            result = subprocess.run([
                str(xdk / 'bin/win32/cl.exe'), '/nologo', '/c', '/O2', '/EHsc',
                '/D_XBOX', '/D_XBOX_VER=200', '/I' + str(xdk / 'include/xbox'),
                '/Fo' + str(output), str(source),
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(output.read_bytes()[:2], b'\xf2\x01')


if __name__ == '__main__':
    unittest.main()
