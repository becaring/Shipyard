import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'python'))
from xbox360_platform import lus_resources, resource_lookup

LOCK = 'const Shipyard::Async::lock_guard<Shipyard::Async::mutex> lock(mMutex);'
MANAGER = '''ResourceManager::ResourceManager() : mAltAssetsEnabled(false) {
}

std::shared_ptr<IResource> ResourceManager::LoadResourceProcess(const ResourceIdentifier& identifier, bool loadExact,
                                                                std::shared_ptr<ResourceInitData> initData) {
    const bool isAltPath = false;
    auto cacheLine = CheckCache(identifier, loadExact);
    auto cachedResource = GetCachedResource(cacheLine);
    if (cachedResource != nullptr) {
        return cachedResource;
    }

    // Check for resource load errors which can indicate an alternate asset.
    auto file = LoadFileProcess(identifier.Path);
    if (file == nullptr) {
        mResourceCache[identifier] = ResourceLoadError::NotFound;
        return nullptr;
    }
    auto resource = GetResourceLoader()->LoadResource(identifier.Path, file, initData);
    cachedResource = GetCachedResource(identifier, true);

    {
        LOCK

        if (cachedResource != nullptr) {
            // If another thread has already loaded this resource, discard the work we already did and return from
            // cache.
            resource = cachedResource;
        }

        // Set the cache to the loaded resource
        if (resource != nullptr) {
            mResourceCache[identifier] = resource;
        } else {
            mResourceCache[identifier] = ResourceLoadError::NotFound;
        }
    }

    if (resource != nullptr) {
        SPDLOG_TRACE("Loaded Resource {} on ResourceManager", identifier.Path);
    }
    return resource;
}

xbox_compat::shared_future<std::shared_ptr<IResource>>
ResourceManager::LoadResourceAsync(const ResourceIdentifier& identifier, bool loadExact, BS::priority_t priority,
                                   std::shared_ptr<ResourceInitData> initData) {
    if (OtrSignatureCheck(identifier.Path.c_str())) {
        auto newFilePath = identifier.Path.substr(7);
        return LoadResourceAsync(ResourceIdentifier(newFilePath, identifier.Owner, identifier.Parent), loadExact, priority);
    }
    return mThreadPool->submit_task(Work, priority);
}

std::shared_ptr<IResource> ResourceManager::LoadResource(const ResourceIdentifier& identifier, bool loadExact,
                                                         std::shared_ptr<ResourceInitData> initData) {
    auto resource = LoadResourceAsync(identifier, loadExact, BS::pr::highest, initData).get();
    return resource;
}

void ResourceManager::DirtyResources(const ResourceFilter& filter) {
}

size_t ResourceManager::UnloadResource(const ResourceIdentifier& identifier) {
    xbox_compat::variant<ResourceLoadError, std::shared_ptr<IResource>> value = nullptr;
    size_t ret = 0;
    if (mResourceCache.contains(identifier)) {
        LOCK
        mResourceCache.erase(identifier);
    }
    return ret;
}

void ResourceManager::SetAltAssetsEnabled(bool isEnabled) {
    mAltAssetsEnabled = isEnabled;
}

bool ResourceManager::IsAltAssetsEnabled() {
    return mAltAssetsEnabled;
}
'''.replace('LOCK', LOCK)

ARCHIVES = '''std::shared_ptr<File> ArchiveManager::LoadFile(uint64_t hash) {
    auto archive = mFileToArchive[hash];
    if (archive == nullptr) {
        return nullptr;
    }
    return archive->LoadFile(hash);
}
std::shared_ptr<Archive> ArchiveManager::GetArchiveFromFile(const std::string& filePath) {
    return mFileToArchive[CRC64(filePath.c_str())];
}
std::shared_ptr<Archive> ArchiveManager::AddArchive(std::shared_ptr<Archive> archive) {
    const auto fileList = archive->ListFiles();
    for (auto& [hash, filename] : *fileList.get()) {
        mHashes[hash] = filename;
        mFileToArchive[hash] = archive;
    }
}
'''

READ = '''    auto fileToLoad = std::make_shared<File>();
    fileToLoad->Buffer = std::make_shared<std::vector<char>>(zipEntryStat.size);

    if (zip_fread(zipEntryFile, fileToLoad->Buffer->data(), zipEntryStat.size) < 0) {
        SPDLOG_TRACE("Error reading file {} in zip archive  {}.", filePath, GetPath());
    }

    if (zip_fclose(zipEntryFile) != 0) {
        SPDLOG_TRACE("Error closing file {} in zip archive  {}.", filePath, GetPath());
    }
'''
SINGLE = ('namespace Ship {\nO2rArchive::O2rArchive(const std::string& archivePath) : Archive(archivePath) {\n}\n'
          'std::shared_ptr<File> O2rArchive::LoadFile(const std::string& filePath) {\n' + READ +
          '    return fileToLoad;\n}\n'
          'bool O2rArchive::WriteFile(const std::string& filePath, const std::vector<uint8_t>& data) {\n'
          '    return zip_close(mZipArchive) == 0;\n}\n}\n')
POOLED = ('std::shared_ptr<File> O2rArchive::LoadFile(const std::string& filePath) {\n'
          '    zip_t* zipArchive = GetZipHandle(); // mZipArchivePool\n' + READ +
          '\n    ReleaseZipHandle(zipArchive);\n    return fileToLoad;\n}\n')


def body(text, signature):
    start = text.index(signature)
    return text[start:text.index('\n}\n', start)]


class ConcurrentCacheTests(unittest.TestCase):
    def check(self, text, generation):
        process = body(text, 'ResourceManager::LoadResourceProcess(')
        missing = process[process.index('if (file == nullptr)'):process.index('auto resource =')]
        self.assertLess(missing.index('lock(mMutex)'), missing.index('= ResourceLoadError::NotFound;'))
        self.assertEqual('InterlockedIncrement' in missing, generation)
        publish = process[process.index('const auto published'):]
        self.assertLess(publish.index('cachedResource = current;'), publish.index('resource = cachedResource;'))
        unload = body(text, 'size_t ResourceManager::UnloadResource(')
        self.assertLess(unload.index('lock(mMutex)'), unload.index('mResourceCache.find(identifier)'))
        self.assertLess(unload.index('value = found->second;'), unload.index('mResourceCache.erase(found);'))
        self.assertIn('ret = 1;', unload)
        self.assertEqual('InterlockedIncrement' in unload, generation)

    def test_upstream_and_lowered_forms(self):
        self.check(lus_resources.concurrent_cache(MANAGER), False)
        for lowered in ('mResourceCache.find(identifier) != mResourceCache.end()',
                        '(mResourceCache.find(identifier) != mResourceCache.end())'):
            smash = MANAGER.replace('mResourceCache.contains(identifier)', lowered)
            smash = smash.replace('Shipyard::Async::lock_guard<Shipyard::Async::mutex>',
                                  'xbox_compat::lock_guard<xbox_compat::mutex>')
            text = lus_resources.concurrent_cache(smash)
            self.check(text, False)
            self.assertNotIn('Shipyard::Async::lock_guard', text)

    def test_runs_after_legacy_lookup_generation(self):
        text = resource_lookup.lookup_generation_source(MANAGER, stable_fills=False)
        self.check(lus_resources.concurrent_cache(text), True)

    def test_anchor_drift_is_rejected(self):
        with self.assertRaises(ValueError):
            lus_resources.concurrent_cache(MANAGER.replace('// cache.\n', '// the cache.\n'))
        with self.assertRaises(ValueError):
            lus_resources.concurrent_cache(MANAGER.replace('mResourceCache.erase(identifier);', 'Erase();'))


class ArchiveTests(unittest.TestCase):
    def test_lookups_never_insert_and_indices_size_once(self):
        text = lus_resources.archive_rehash(lus_resources.archive_find(ARCHIVES))
        self.assertNotIn('mFileToArchive[hash];', text.split('AddArchive')[0])
        self.assertNotIn('mFileToArchive[CRC64', text)
        add = body(text, 'ArchiveManager::AddArchive(')
        self.assertLess(add.index('mHashes.rehash('), add.index('for (auto&'))
        self.assertIn('mFileToArchive.rehash(', add)
        with self.assertRaises(ValueError):
            lus_resources.archive_find(text)

    def test_partial_reads_complete_or_fail(self):
        single = lus_resources.o2r_partial_read(SINGLE, handle_pool=False)
        self.assertIn('while (bytesRead < zipEntryStat.size)', single)
        self.assertIn('zip_fclose(zipEntryFile);\n            return nullptr;', single)
        pooled = lus_resources.o2r_partial_read(POOLED, handle_pool=True)
        self.assertIn('zip_discard(zipArchive);\n            return nullptr;', pooled)
        self.assertLess(pooled.index('readTotal += '), pooled.index('ReleaseZipHandle(zipArchive);'))
        self.assertEqual(pooled.count('ReleaseZipHandle(zipArchive);'), 1)
        with self.assertRaises(ValueError):
            lus_resources.o2r_partial_read(single, handle_pool=False)

    def test_single_handle_reads_and_writes_serialize(self):
        text = lus_resources.o2r_zip_mutex(SINGLE)
        self.assertTrue(text.startswith('#include "async/native.h"\n'))
        self.assertEqual(text.count('zipLock(sXboxZipMutex);'), 2)
        load = body(text, 'O2rArchive::LoadFile(')
        self.assertLess(load.index('zipLock'), load.index('zip_fread'))
        with self.assertRaises(ValueError):
            lus_resources.o2r_zip_mutex(POOLED)


class LookupTests(unittest.TestCase):
    def test_synchronous_misses_load_inline(self):
        text = resource_lookup.synchronous_misses(resource_lookup.synchronous_cache_hit(MANAGER))
        load = body(text, 'ResourceManager::LoadResource(const ResourceIdentifier&')
        self.assertNotIn('.get()', load)
        self.assertLess(load.index('if (cached) return cached;'), load.index('LoadResourceProcess('))
        # Signed paths keep the asynchronous normalization: loadExact kept, no init data.
        self.assertIn('identifier.Parent),\n                                       loadExact);', load)
        self.assertIn('resource = LoadResourceProcess(identifier, loadExact, initData);', load)
        forwarded = MANAGER.replace('loadExact, priority);', 'loadExact, priority, initData);')
        load = body(resource_lookup.synchronous_misses(forwarded), 'ResourceManager::LoadResource(const')
        self.assertIn('loadExact, initData);\n    } else', load)
        with self.assertRaises(ValueError):
            resource_lookup.synchronous_misses(MANAGER.replace('loadExact, priority);', 'priority);'))

    def test_recorded_miss_returns_before_probe(self):
        text = resource_lookup.recorded_misses(MANAGER)
        process = body(text, 'ResourceManager::LoadResourceProcess(')
        self.assertLess(process.index('== ResourceLoadError::NotFound) {\n        return nullptr;'),
                        process.index('LoadFileProcess('))

    def test_stable_generation_bumps_only_on_changed_answers(self):
        text = resource_lookup.lookup_generation_source(MANAGER)
        process = body(text, 'ResourceManager::LoadResourceProcess(')
        missing = process[process.index('if (file == nullptr)'):process.index('auto resource =')]
        self.assertNotIn('InterlockedIncrement', missing)
        store = process[process.index('xboxPrevious = NULL;'):]
        self.assertLess(store.index('previous->second'), store.index('mResourceCache[identifier] = resource;'))
        self.assertIn('if (xboxPrevious ? xboxPrevious != resource.get() :', store)
        self.assertIn('IResource::gAltAssetPrefix) == 0)\n            InterlockedIncrement', store)
        self.assertIn('if (mAltAssetsEnabled != isEnabled)\n        InterlockedIncrement', text)
        legacy = resource_lookup.lookup_generation_source(MANAGER, stable_fills=False)
        self.assertNotIn('xboxPrevious', legacy)
        self.assertEqual(legacy.count('InterlockedIncrement'), 5)

    def test_borrowed_keys_require_borrowed_identifiers(self):
        with self.assertRaises(ValueError):
            resource_lookup.borrowed_lookup_keys(MANAGER)


if __name__ == '__main__':
    unittest.main()
