"""Keep complete asset-path distinctions in the in-memory resource cache."""


def _once(source, old, new):
    if source.count(old) != 1:
        raise ValueError('resource lookup source anchor changed: ' + old[:70])
    return source.replace(old, new, 1)


def synchronous_cache_hits(source):
    """Return settled synchronous hits without constructing a ready future.

    Probe normalized tagged names with the version's own cache policy. Leave
    misses to its asynchronous path, including its init-data normalization rules.
    """
    signature = 'std::shared_ptr<IResource> ResourceManager::LoadResource(const ResourceIdentifier& identifier, bool loadExact,'
    if source.count(signature) != 1:
        raise ValueError('synchronous resource load signature changed')
    normalization = '''auto newFilePath = identifier.Path.substr(7);
        return LoadResourceAsync(ResourceIdentifier(newFilePath, identifier.Owner, identifier.Parent), loadExact, priority);'''
    if source.count(normalization) != 1:
        raise ValueError('asynchronous resource normalization policy changed')
    start = source.index(signature)
    end = source.index('\n}', start) + 2
    body = _once(source[start:end],
        '    auto resource = LoadResourceAsync(identifier, loadExact, BS::pr::highest, initData).get();',
        '''    std::shared_ptr<IResource> cached;
    if (OtrSignatureCheck(identifier.Path.c_str())) {
        std::string normalizedPath = identifier.Path.substr(7);
        while (OtrSignatureCheck(normalizedPath.c_str())) normalizedPath.erase(0, 7);
        cached = GetCachedResource(ResourceIdentifier(normalizedPath, identifier.Owner, identifier.Parent), loadExact);
    } else {
        cached = GetCachedResource(identifier, loadExact);
    }
    if (cached) return cached;
    auto resource = LoadResourceAsync(identifier, loadExact, BS::pr::highest, initData).get();''')
    return source[:start] + body + source[end:]


def synchronous_misses(source):
    """Load a synchronous miss on the calling thread instead of a pool worker.

    Handing it to a worker and blocking on the future costs two cross-core
    wakeups per resource and waits behind the worker's current (prefetch) job;
    a pool task loading a resource with one worker would deadlock. The
    worker's body is LoadResourceProcess itself, so this keeps its cache,
    alternate and first-publish rules, and the asynchronous normalization's
    loadExact and init-data policy for signed paths. Archive reads must be
    safe on any thread: a zip handle pool, or lus_resources.o2r_zip_mutex.
    Run after synchronous_cache_hit(s).
    """
    keep = 'ResourceIdentifier(newFilePath, identifier.Owner, identifier.Parent), loadExact, priority'
    if source.count('LoadResourceAsync(' + keep + ');') == 1:
        signed = ''
    elif source.count('LoadResourceAsync(' + keep + ', initData);') == 1:
        signed = ', initData'
    else:
        raise ValueError('asynchronous resource normalization policy changed')
    signature = 'std::shared_ptr<IResource> ResourceManager::LoadResource(const ResourceIdentifier& identifier, bool loadExact,'
    start = source.index(signature)
    end = source.index('\n}', start) + 2
    body = _once(source[start:end],
        '    auto resource = LoadResourceAsync(identifier, loadExact, BS::pr::highest, initData).get();',
        '''    std::shared_ptr<IResource> resource;
    if (OtrSignatureCheck(identifier.Path.c_str())) {
        std::string normalizedPath = identifier.Path.substr(7);
        while (OtrSignatureCheck(normalizedPath.c_str())) normalizedPath.erase(0, 7);
        resource = LoadResourceProcess(ResourceIdentifier(normalizedPath, identifier.Owner, identifier.Parent),
                                       loadExact''' + signed + ''');
    } else {
        resource = LoadResourceProcess(identifier, loadExact, initData);
    }''')
    return source[:start] + body + source[end:]


def _replace_function(source, signature, replacement):
    if source.count(signature) != 1:
        raise ValueError('resource lookup function changed: ' + signature[:70])
    begin = source.index(signature)
    end = source.index('\n}', begin) + 2
    return source[:begin] + replacement + source[end:]


BORROWED_ALTERNATE = '''    const std::string& prefix = IResource::gAltAssetPrefix;
    if (!loadExact && mAltAssetsEnabled && PATH.compare(0, prefix.size(), prefix) != 0) {
        ResourceIdentifier alternate(PATH.c_str(), OWNER, PARENT, ResourceIdentifier::BorrowedLookup(),
                                     prefix.data(), prefix.size());
        if (alternate.mBorrowedLength != PATH.size()) {
            alternate.mBorrowedLength = PATH.size();
            alternate.mHash = alternate.CalculateHash();
        }
        const auto found = mResourceCache.find(alternate);
        if (found != mResourceCache.end() &&
            xbox_compat::holds_alternative<std::shared_ptr<IResource>>(found->second))
            return found->second;
    }
'''


def borrowed_lookup_keys(source):
    """Borrow cache keys for CheckCache, GetCachedResource(string) and LoadResource(string) hits.

    One lock covers the alternate and base probes. Alternate precedence, dirty
    fallback (GetCachedResource) and owner/archive identity are unchanged;
    signed paths and misses keep the owned-key route. Run after borrowed_cache_source.
    """
    if 'ResourceIdentifier::BorrowedLookup()' not in source:
        raise ValueError('borrowed lookup keys require borrowed_cache_source')
    lock = '    const Shipyard::Async::lock_guard<Shipyard::Async::mutex> lock(mMutex);\n'
    source = _replace_function(source,
        'ResourceManager::CheckCache(const ResourceIdentifier& identifier, bool loadExact)',
        'ResourceManager::CheckCache(const ResourceIdentifier& identifier, bool loadExact) {\n' + lock +
        BORROWED_ALTERNATE.replace('PATH', 'identifier.Path').replace('OWNER', 'identifier.Owner')
                          .replace('PARENT', 'identifier.Parent') + '''    const auto found = mResourceCache.find(identifier);
    if (found == mResourceCache.end()) return ResourceLoadError::NotCached;
    return found->second;
}''')
    source = _replace_function(source,
        'ResourceManager::CheckCache(const std::string& filePath, bool loadExact)',
        'ResourceManager::CheckCache(const std::string& filePath, bool loadExact) {\n' + lock +
        BORROWED_ALTERNATE.replace('PATH', 'filePath').replace('OWNER', 'mDefaultCacheOwner')
                          .replace('PARENT', 'mDefaultCacheArchive') + '''    ResourceIdentifier key(filePath.c_str(), mDefaultCacheOwner, mDefaultCacheArchive,
                           ResourceIdentifier::BorrowedLookup());
    if (key.mBorrowedLength != filePath.size()) {
        key.mBorrowedLength = filePath.size();
        key.mHash = key.CalculateHash();
    }
    const auto found = mResourceCache.find(key);
    if (found == mResourceCache.end()) return ResourceLoadError::NotCached;
    return found->second;
}''')
    signature = 'std::shared_ptr<IResource> ResourceManager::GetCachedResource(const std::string& filePath, bool loadExact)'
    source = _replace_function(source, signature, signature + ''' {
    return GetCachedResource(CheckCache(filePath, loadExact));
}''')
    signature = 'std::shared_ptr<IResource> ResourceManager::LoadResource(const std::string& filePath, bool loadExact,'
    begin = source.index(signature)
    end = source.index('\n}', begin)
    forward = 'return LoadResource(ResourceIdentifier(filePath, mDefaultCacheOwner, mDefaultCacheArchive), loadExact, initData);'
    if source[begin:end].count(forward) != 1:
        raise ValueError('string resource load forwarding changed')
    return source[:begin] + source[begin:end].replace('    ' + forward, '''    if (!OtrSignatureCheck(filePath.c_str())) {
        std::shared_ptr<IResource> cached = GetCachedResource(filePath, loadExact);
        if (cached) return cached;
    }
    ''' + forward) + source[end:]


def recorded_misses(source):
    """A recorded NotFound returns before probing the archive again.

    Archive contents do not change under a recorded miss: archives are added
    at boot, a written file already had an archive, and unloads erase the
    entry first. Only a genuine missing file may record NotFound, so archive
    reads must never fail transiently (see lus_resources).
    """
    anchor = '    // Check for resource load errors which can indicate an alternate asset.\n'
    if source.count(anchor) != 1 or 'auto cacheLine = CheckCache(identifier, loadExact);' not in source:
        raise ValueError('resource miss shortcut anchor changed')
    return _once(source, anchor, '''    // Archives do not change under a recorded miss; unloads erase the entry first.
    if (xbox_compat::holds_alternative<ResourceLoadError>(cacheLine) &&
        xbox_compat::get<ResourceLoadError>(cacheLine) == ResourceLoadError::NotFound) {
        return nullptr;
    }

''' + anchor)


def borrowed_cache_header(source):
    source = '#include "resources/path_span.h"\n' + source
    source = _once(source,
        'GetCachedResource(xbox_compat::variant<ResourceLoadError, std::shared_ptr<IResource>> cacheLine)',
        'GetCachedResource(const xbox_compat::variant<ResourceLoadError, std::shared_ptr<IResource>>& cacheLine)')
    source = _once(source, '    friend struct ResourceIdentifierHash;',
        '    friend struct ResourceIdentifierHash;\n    friend class ResourceManager;')
    source = _once(source, '    bool operator==(const ResourceIdentifier& rhs) const;',
        '    ResourceIdentifier(const ResourceIdentifier& other);\n'
        '    bool operator==(const ResourceIdentifier& rhs) const;')
    source = _once(source, '    size_t mHash;', '''    size_t mHash;
    struct BorrowedLookup {};
    // Only synchronous map lookup borrows. Copying a key always owns its path.
    ResourceIdentifier(const char* path, uintptr_t owner,
                       const std::shared_ptr<Archive>& parent, BorrowedLookup,
                       const char* prefix = "", size_t prefixLength = 0);
    const char* mBorrowedPath;
    size_t mBorrowedLength;
    const char* mBorrowedPrefix;
    size_t mBorrowedPrefixLength;
    Shipyard::Resources::PathSpan LookupView() const {
        return mBorrowedPath ? Shipyard::Resources::PathSpan(mBorrowedPrefix, mBorrowedPrefixLength,
                                                           mBorrowedPath, mBorrowedLength)
                             : Shipyard::Resources::PathSpan(Path.data(), Path.size());
    }''')
    anchor = '    std::shared_ptr<IResource> LoadResourceProcess(const std::string& filePath, bool loadExact = false,'
    return _once(source, anchor, '''    std::shared_ptr<IResource> LoadResourceProcess(const char* filePath, bool loadExact = false,
                                                   std::shared_ptr<ResourceInitData> initData = nullptr);
''' + anchor)


def borrowed_cache_source(source, *, owned_rvalue_constructor=False):
    signature = 'ResourceManager::GetCachedResource(xbox_compat::variant<ResourceLoadError, std::shared_ptr<IResource>> cacheLine)'
    begin = source.index(signature)
    end = source.index('\n}', begin) + 2
    body = _once(source[begin:end], signature,
        'ResourceManager::GetCachedResource(const xbox_compat::variant<ResourceLoadError, std::shared_ptr<IResource>>& cacheLine)')
    body = _once(body, 'auto resource = xbox_compat::get<std::shared_ptr<IResource>>(cacheLine);',
        'const std::shared_ptr<IResource>& resource = xbox_compat::get<std::shared_ptr<IResource>>(cacheLine);')
    source = source[:begin] + body + source[end:]
    begin = source.index('ResourceIdentifier::ResourceIdentifier(')
    end = source.index('\n}', begin) + 2
    body = _once(source[begin:end], ': Path(path), Owner(owner), Parent(parent) {',
                 ': Path(path), Owner(owner), Parent(parent), mBorrowedPath(NULL), mBorrowedLength(0), mBorrowedPrefix(""), mBorrowedPrefixLength(0) {')
    source = source[:begin] + body + '''

ResourceIdentifier::ResourceIdentifier(const ResourceIdentifier& other)
    : Path(other.LookupView().Owned()), Owner(other.Owner), Parent(other.Parent),
      mHash(other.mHash), mBorrowedPath(NULL), mBorrowedLength(0), mBorrowedPrefix(""), mBorrowedPrefixLength(0) {
}

ResourceIdentifier::ResourceIdentifier(const char* path, uintptr_t owner,
                                       const std::shared_ptr<Archive>& parent, BorrowedLookup,
                                       const char* prefix, size_t prefixLength)
    : Path(), Owner(owner), Parent(parent), mBorrowedPath(path), mBorrowedLength(std::strlen(path)),
      mBorrowedPrefix(prefix), mBorrowedPrefixLength(prefixLength) {
    mHash = CalculateHash();
}''' + source[end:]
    rvalue = ': Path(std::move(path)), Owner(owner), Parent(parent) {'
    if owned_rvalue_constructor:
        source = _once(source, rvalue,
            ': Path(std::move(path)), Owner(owner), Parent(parent), mBorrowedPath(NULL), '
            'mBorrowedLength(0), mBorrowedPrefix(""), mBorrowedPrefixLength(0) {')
    elif rvalue in source:
        raise ValueError('ResourceIdentifier rvalue constructor requires an explicit capability')
    source = _once(source, 'return Owner == rhs.Owner && Path == rhs.Path && Parent == rhs.Parent;',
        'return Owner == rhs.Owner && LookupView() == rhs.LookupView() && Parent == rhs.Parent;')
    source = _once(source, 'Shipyard::Resources::PathHash()(Path)',
                   'LookupView().Hash()')
    anchor = 'std::shared_ptr<IResource> ResourceManager::LoadResourceProcess(const std::string& filePath'
    start = source.index(anchor)
    source = source[:start] + '''std::shared_ptr<IResource> ResourceManager::LoadResourceProcess(const char* filePath, bool loadExact,
                                                                std::shared_ptr<ResourceInitData> initData) {
    while (OtrSignatureCheck(filePath)) {
        filePath += 7;
        loadExact = false;
    }
    // Only settled cache hits bypass the loader. Missing, dirty and null alternate
    // resources still take its authoritative load/error path after releasing the lock.
    {
        const std::string& prefix = IResource::gAltAssetPrefix;
        const bool tryAlternate = !loadExact && mAltAssetsEnabled &&
            std::strncmp(filePath, prefix.c_str(), prefix.size()) != 0;
        bool tryBase = !tryAlternate;
        const Shipyard::Async::lock_guard<Shipyard::Async::mutex> lock(mMutex);
        if (tryAlternate) {
            ResourceIdentifier alternate(filePath, mDefaultCacheOwner, mDefaultCacheArchive,
                ResourceIdentifier::BorrowedLookup(), prefix.data(), prefix.size());
            const auto altFound = mResourceCache.find(alternate);
            if (altFound != mResourceCache.end()) {
                if (xbox_compat::holds_alternative<std::shared_ptr<IResource>>(altFound->second)) {
                    const std::shared_ptr<IResource>& resource =
                        xbox_compat::get<std::shared_ptr<IResource>>(altFound->second);
                    if (resource != nullptr && resource.use_count() > 0 && !resource->IsDirty())
                        return resource;
                } else if (xbox_compat::holds_alternative<ResourceLoadError>(altFound->second)) {
                    tryBase = xbox_compat::get<ResourceLoadError>(altFound->second) != ResourceLoadError::NotCached;
                }
            }
        }
        if (tryBase) {
        ResourceIdentifier key(filePath, mDefaultCacheOwner, mDefaultCacheArchive,
                               ResourceIdentifier::BorrowedLookup());
        const auto found = mResourceCache.find(key);
        if (found != mResourceCache.end() &&
            xbox_compat::holds_alternative<std::shared_ptr<IResource>>(found->second)) {
            const std::shared_ptr<IResource>& resource =
                xbox_compat::get<std::shared_ptr<IResource>>(found->second);
            if (resource != nullptr && resource.use_count() > 0 && !resource->IsDirty())
                return resource;
        }
        }
    }
    return LoadResourceProcess(std::string(filePath), loadExact, initData);
}

''' + source[start:]
    return '#include <cstring>\n' + source


def drain_header(source):
    return _once(source, '    ~ResourceManager();',
        '    ~ResourceManager();\n    void WaitForPendingTasks();')


def drain_source(source):
    return _once(source, 'ResourceManager::~ResourceManager() {', '''void ResourceManager::WaitForPendingTasks() {
    // Called by the application thread after producers stop, while Context lives.
    if (mThreadPool) {
        mThreadPool->unpause();
        mThreadPool->wait();
    }
}

ResourceManager::~ResourceManager() {
    WaitForPendingTasks();
    // Workers must leave before the cache mutex and other members are destroyed.
    mThreadPool.reset();''')


def stateless_signature(source):
    return _once(source,
        'return Ship::Context::GetInstance()->GetResourceManager()->OtrSignatureCheck(imgData);',
        'return std::strncmp(imgData, "__OTR__", 7) == 0;')


def path_hash(source):
    begin = source.index('size_t ResourceIdentifier::CalculateHash() {')
    end = source.index('\n}', begin) + 2
    body = source[begin:end]
    old = 'std::hash<std::string>()'
    if body.count(old) != 2:
        raise ValueError('resource path hash inventory changed')
    body = body.replace(old, 'Shipyard::Resources::PathHash()')
    return '#include "resources/path_hash.h"\n' + source[:begin] + body + source[end:]


def texture_hash_lookup(source):
    begin = source.index('bool gfx_set_timg_otr_hash_handler_custom(')
    end = source.index('\nbool ', begin + 5)
    body = source[begin:end]
    lookup = 'Ship::Context::GetInstance()->GetResourceManager()->GetArchiveManager()->HashToCString(hash)'
    if body.count(lookup) != 2 or 'if (fileName == nullptr)' not in body:
        raise ValueError('texture hash lookup inventory changed')
    first = body.index(lookup)
    second = body.index(lookup, first + len(lookup))
    body = body[:second] + 'fileName' + body[second + len(lookup):]
    return source[:begin] + body + source[end:]


def texture_command_manager(source):
    begin = source.index('bool gfx_set_timg_otr_hash_handler_custom(')
    end = source.index('\nbool ', begin + 5)
    body = source[begin:end]
    manager = 'Ship::Context::GetInstance()->GetResourceManager()'
    if body.count(manager) != 2:
        raise ValueError('texture command manager acquisition inventory changed')
    body = _once(body, '    const char* fileName = ',
        '    const auto resourceManager = ' + manager + ';\n    const char* fileName = ')
    # Keep the owning manager for this synchronous command only.
    first = body.index(manager) + len(manager)
    body = body[:first] + body[first:].replace(manager, 'resourceManager')
    return source[:begin] + body + source[end:]


def lookup_generation_header(source):
    source = _once(source, '    bool IsAltAssetsEnabled();',
        '    unsigned GetXboxLookupGeneration() const;\n    bool IsAltAssetsEnabled();')
    return _once(source, '    bool mAltAssetsEnabled;',
        '    volatile LONG mXboxLookupGeneration;\n    bool mAltAssetsEnabled;')


def lookup_generation_source(source, *, stable_fills=True):
    """Advance the lookup generation whenever a settled path->resource answer can change.

    Generation-keyed caches store only positive answers and also check IsDirty().
    With stable_fills, a missing-file NotFound and a first fill of a base path keep
    the generation: no settled lookup returned a resource for that key, and an
    alternate answer outranks its base. A first alternate fill, replacing a
    different resource, unloads, dirtying and alt toggles still advance it.
    stable_fills=False keeps the older bump-on-every-load behaviour.
    """
    source = _once(source, 'ResourceManager::ResourceManager() : mAltAssetsEnabled(false)',
        'ResourceManager::ResourceManager() : mXboxLookupGeneration(1), mAltAssetsEnabled(false)')
    if stable_fills:
        source = _once(source, '        // Set the cache to the loaded resource\n',
            '        // An address only: owning the replaced resource would destroy it under the lock.\n'
            '        const IResource* xboxPrevious = NULL;\n'
            '        {\n'
            '            const auto previous = mResourceCache.find(identifier);\n'
            '            if (previous != mResourceCache.end() &&\n'
            '                xbox_compat::holds_alternative<std::shared_ptr<IResource>>(previous->second))\n'
            '                xboxPrevious = xbox_compat::get<std::shared_ptr<IResource>>(previous->second).get();\n'
            '        }\n'
            '        // Set the cache to the loaded resource\n')
        source = _once(source, '    }\n\n    if (resource != nullptr) {\n        SPDLOG_TRACE("Loaded Resource',
            '        // A first alternate fill changes the answer for its base path.\n'
            '        if (xboxPrevious ? xboxPrevious != resource.get() :\n'
            '            resource != nullptr && identifier.Path.compare(0, IResource::gAltAssetPrefix.size(),\n'
            '                                                           IResource::gAltAssetPrefix) == 0)\n'
            '            InterlockedIncrement(&mXboxLookupGeneration);\n    }\n\n'
            '    if (resource != nullptr) {\n        SPDLOG_TRACE("Loaded Resource')
    else:
        source = _once(source, '        mResourceCache[identifier] = ResourceLoadError::NotFound;\n        return nullptr;',
            '        mResourceCache[identifier] = ResourceLoadError::NotFound;\n'
            '        InterlockedIncrement(&mXboxLookupGeneration);\n        return nullptr;')
        source = _once(source, '    }\n\n    if (resource != nullptr) {\n        SPDLOG_TRACE("Loaded Resource',
            '        InterlockedIncrement(&mXboxLookupGeneration);\n    }\n\n'
            '    if (resource != nullptr) {\n        SPDLOG_TRACE("Loaded Resource')
    source = _once(source, 'void ResourceManager::DirtyResources(const ResourceFilter& filter) {',
        'void ResourceManager::DirtyResources(const ResourceFilter& filter) {\n'
        '    InterlockedIncrement(&mXboxLookupGeneration);')
    source = _once(source, '        mResourceCache.erase(identifier);',
        '        mResourceCache.erase(identifier);\n'
        '        InterlockedIncrement(&mXboxLookupGeneration);')
    source = _once(source, 'void ResourceManager::SetAltAssetsEnabled(bool isEnabled) {\n'
        '    mAltAssetsEnabled = isEnabled;',
        'void ResourceManager::SetAltAssetsEnabled(bool isEnabled) {\n'
        '    if (mAltAssetsEnabled != isEnabled)\n'
        '        InterlockedIncrement(&mXboxLookupGeneration);\n'
        '    mAltAssetsEnabled = isEnabled;')
    source = _once(source, 'bool ResourceManager::IsAltAssetsEnabled() {',
        'unsigned ResourceManager::GetXboxLookupGeneration() const {\n'
        '    return static_cast<unsigned>(InterlockedCompareExchange(\n'
        '        const_cast<volatile LONG*>(&mXboxLookupGeneration), 0, 0));\n'
        '}\n\nbool ResourceManager::IsAltAssetsEnabled() {')
    return source


def texture_hash_phase_profile(source):
    """Time the hash route without changing its lookup decisions."""
    begin = source.index('bool gfx_set_timg_otr_hash_handler_custom(')
    end = source.index('\nbool ', begin + 5)
    body = source[begin:end]
    if 'MODERN TEXTURE HASH PHASE PROFILE' in body:
        raise ValueError('texture hash phase profile already projected')
    body = _once(body,
        '    const char* fileName = resourceManager->GetArchiveManager()->HashToCString(hash);',
        '    // MODERN TEXTURE HASH PHASE PROFILE\n'
        '    ModernFrameProfile::TextureHashSample xboxTextureHashSample;\n'
        '    ModernFrameProfile::BeginTextureHash(xboxTextureHashSample);\n'
        '    const char* fileName = resourceManager->GetArchiveManager()->HashToCString(hash);\n'
        '    ModernFrameProfile::AdvanceTextureHash(\n'
        '        xboxTextureHashSample, ModernFrameProfile::TextureHashFinal);')
    body = _once(body,
        '    if (fileName == nullptr) {\n        (*cmd0)++;\n        return false;\n    }',
        '    if (fileName == nullptr) {\n'
        '        ModernFrameProfile::EndTextureHash(xboxTextureHashSample);\n'
        '        (*cmd0)++;\n        return false;\n    }\n'
        '    ModernFrameProfile::AdvanceTextureHash(\n'
        '        xboxTextureHashSample, ModernFrameProfile::TextureHashOwningCache);')
    body = _once(body,
        '    if (!texture) {\n        texture = std::static_pointer_cast<Fast::Texture>(',
        '    if (!texture) {\n'
        '        ModernFrameProfile::AdvanceTextureHash(\n'
        '            xboxTextureHashSample, ModernFrameProfile::TextureHashLoader);\n'
        '        texture = std::static_pointer_cast<Fast::Texture>(')
    body = _once(body,
        '            gfx->mXboxTextureHashCache.Store(hash, fileName, lookupGeneration, texture);\n    }',
        '            gfx->mXboxTextureHashCache.Store(hash, fileName, lookupGeneration, texture);\n'
        '    }\n'
        '    ModernFrameProfile::AdvanceTextureHash(\n'
        '        xboxTextureHashSample, ModernFrameProfile::TextureHashFinal);')
    body = _once(body,
        '    (*cmd0)++;\n    return false;\n}',
        '    (*cmd0)++;\n'
        '    ModernFrameProfile::EndTextureHash(xboxTextureHashSample);\n'
        '    return false;\n}')
    return source[:begin] + body + source[end:]


def texture_hash_borrowed_handler(source):
    """Borrow a cache hit only for this synchronous texture command.

    The metadata assignment still takes its own shared ownership before the
    handler returns. A miss keeps the loader's owning shared_ptr, including
    its existing dirty and generation checks.
    """
    begin = source.index('bool gfx_set_timg_otr_hash_handler_custom(')
    end = source.index('\nbool ', begin + 5)
    body = source[begin:end]
    body = _once(body,
        '''    std::shared_ptr<Fast::Texture> texture =
        gfx->mXboxTextureHashCache.Lookup(hash, fileName, lookupGeneration);''',
        '''    std::shared_ptr<Fast::Texture> loadedTexture;
    const std::shared_ptr<Fast::Texture>* cachedTexture =
        gfx->mXboxTextureHashCache.LookupBorrowed(hash, fileName, lookupGeneration);
    const std::shared_ptr<Fast::Texture>& texture =
        cachedTexture ? *cachedTexture : loadedTexture;''')
    body = _once(body,
        '        texture = std::static_pointer_cast<Fast::Texture>(',
        '        loadedTexture = std::static_pointer_cast<Fast::Texture>(')
    return source[:begin] + body + source[end:]


def texture_hash_epoch_handler(source):
    """Use a verified archive epoch to avoid resolving a warm hash to a path.

    The archive manager owns the hash table and its epoch covers every map
    mutation in Modern's archive projector. A mismatch still calls the
    authoritative HashToCString and path-aware texture cache route.
    """
    begin = source.index('bool gfx_set_timg_otr_hash_handler_custom(')
    end = source.index('\nbool ', begin + 5)
    body = source[begin:end]
    body = _once(body,
        '    const char* fileName = resourceManager->GetArchiveManager()->HashToCString(hash);',
        '''    Interpreter* gfx = XboxDispatchInterpreter();
    const auto archiveManager = resourceManager->GetArchiveManager();
    const unsigned hashEpoch = archiveManager->GetXboxHashEpoch();
    const unsigned lookupGeneration = resourceManager->GetXboxLookupGeneration();
    const auto* archiveHit = gfx->mXboxTextureHashCache.LookupArchive(
        hash, archiveManager.get(), hashEpoch, lookupGeneration);
    if (archiveHit && archiveManager->GetXboxHashEpoch() != hashEpoch)
        archiveHit = NULL;
    const char* fileName = archiveHit ? archiveHit->path.c_str() : archiveManager->HashToCString(hash);''')
    body = _once(body,
        '''    Interpreter* gfx = XboxDispatchInterpreter();
    const unsigned lookupGeneration = resourceManager->GetXboxLookupGeneration();
    std::shared_ptr<Fast::Texture> loadedTexture;
    const std::shared_ptr<Fast::Texture>* cachedTexture =
        gfx->mXboxTextureHashCache.LookupBorrowed(hash, fileName, lookupGeneration);''',
        '''    std::shared_ptr<Fast::Texture> loadedTexture;
    const auto* pathHit = archiveHit;
    if (!pathHit && archiveManager->GetXboxHashEpoch() == hashEpoch)
        pathHit = gfx->mXboxTextureHashCache.LookupAndCertifyArchive(
            hash, fileName, lookupGeneration, archiveManager.get(), hashEpoch);
    const std::shared_ptr<Fast::Texture>* cachedTexture =
        pathHit ? &pathHit->texture : NULL;''')
    body = _once(body,
        '''        if (texture && !texture->IsDirty() &&
            resourceManager->GetXboxLookupGeneration() == lookupGeneration)
            gfx->mXboxTextureHashCache.Store(hash, fileName, lookupGeneration, texture);''',
        '''        if (texture && !texture->IsDirty() &&
            resourceManager->GetXboxLookupGeneration() == lookupGeneration &&
            archiveManager->GetXboxHashEpoch() == hashEpoch &&
            resourceManager->GetArchiveManager().get() == archiveManager.get())
            gfx->mXboxTextureHashCache.StoreArchive(
                hash, fileName, lookupGeneration, archiveManager.get(), hashEpoch, texture);''')
    return source[:begin] + body + source[end:]


def texture_hash_cache_handler(source):
    begin = source.index('bool gfx_set_timg_otr_hash_handler_custom(')
    end = source.index('\nbool ', begin + 5)
    body = source[begin:end]
    old = '''    std::shared_ptr<Fast::Texture> texture =
        std::static_pointer_cast<Fast::Texture>(resourceManager->LoadResourceProcess(
            fileName));'''
    new = '''    Interpreter* gfx = XboxDispatchInterpreter();
    const unsigned lookupGeneration = resourceManager->GetXboxLookupGeneration();
    std::shared_ptr<Fast::Texture> texture =
        gfx->mXboxTextureHashCache.Lookup(hash, fileName, lookupGeneration);
    if (!texture) {
        texture = std::static_pointer_cast<Fast::Texture>(
            resourceManager->LoadResourceProcess(fileName));
        if (texture && !texture->IsDirty() &&
            resourceManager->GetXboxLookupGeneration() == lookupGeneration)
            gfx->mXboxTextureHashCache.Store(hash, fileName, lookupGeneration, texture);
    }'''
    body = _once(body, old, new)
    body = _once(body, '            Interpreter* gfx = XboxDispatchInterpreter();\n'
        '            gfx->GfxDpSetTextureImage', '            gfx->GfxDpSetTextureImage')
    body = _once(body, '''        // OTRTODO: We have disabled caching for now to fix a texture corruption issue with HD texture
        // support. In doing so, there is a potential performance hit since we are not caching lookups. We
        // need to do proper profiling to see whether or not it is worth it to keep the caching system.

''', '')
    source = texture_hash_phase_profile(source[:begin] + body + source[end:])
    begin = source.index('bool gfx_set_timg_handler_rdp(')
    end = source.index('\nbool ', begin + 5)
    body = source[begin:end]
    old = '''            std::shared_ptr<Fast::Texture> tex = std::static_pointer_cast<Fast::Texture>(
                Ship::Context::GetInstance()->GetResourceManager()->LoadResourceProcess(imgData));'''
    new = '''            const auto resourceManager = Ship::Context::GetInstance()->GetResourceManager();
            const uint64_t cacheKey = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(imgData));
            const unsigned lookupGeneration = resourceManager->GetXboxLookupGeneration();
            std::shared_ptr<Fast::Texture> tex =
                gfx->mXboxTextureHashCache.Lookup(cacheKey, imgData, lookupGeneration);
            if (!tex) {
                tex = std::static_pointer_cast<Fast::Texture>(
                    resourceManager->LoadResourceProcess(imgData));
                if (tex && !tex->IsDirty() &&
                    resourceManager->GetXboxLookupGeneration() == lookupGeneration)
                    gfx->mXboxTextureHashCache.Store(cacheKey, imgData, lookupGeneration, tex);
            }'''
    body = _once(body, old, new)
    return source[:begin] + body + source[end:]


def synchronous_cache_hit(source):
    """Use the version's own cache policy before constructing a future."""
    anchor = '    auto resource = LoadResourceAsync(identifier, loadExact, BS::pr::highest, initData).get();'
    return _once(source, anchor, """    std::shared_ptr<IResource> cached;
    if (OtrSignatureCheck(identifier.Path.c_str())) {
        std::string normalizedPath = identifier.Path.substr(7);
        while (OtrSignatureCheck(normalizedPath.c_str())) normalizedPath.erase(0, 7);
        cached = GetCachedResource(ResourceIdentifier(normalizedPath, identifier.Owner, identifier.Parent), loadExact);
    } else {
        cached = GetCachedResource(identifier, loadExact);
    }
    if (cached) return cached;
""" + anchor)


def settled_alternate_header(source):
    return _once(source, '    ~ResourceManager();',
        '    ~ResourceManager();\n    std::shared_ptr<IResource> GetSettledAlternate(const char* path);')


def settled_alternate_source(source):
    anchor='std::shared_ptr<IResource> ResourceManager::LoadResourceProcess(const char* filePath'
    start=source.index(anchor)
    return source[:start]+"""std::shared_ptr<IResource> ResourceManager::GetSettledAlternate(const char* path) {
    if (!mAltAssetsEnabled) return std::shared_ptr<IResource>();
    if (OtrSignatureCheck(path)) path += 7;
    // The consumer preflight removes only one signature, and explicit alt paths
    // intentionally evict that very resource. Preserve both exceptional paths.
    const std::string& prefix = IResource::gAltAssetPrefix;
    if (OtrSignatureCheck(path) || std::strncmp(path, prefix.c_str(), prefix.size()) == 0)
        return std::shared_ptr<IResource>();
    const Shipyard::Async::lock_guard<Shipyard::Async::mutex> lock(mMutex);
    ResourceIdentifier base(path, mDefaultCacheOwner, mDefaultCacheArchive, ResourceIdentifier::BorrowedLookup());
    if (mResourceCache.find(base) != mResourceCache.end()) return std::shared_ptr<IResource>();
    ResourceIdentifier alternate(path, mDefaultCacheOwner, mDefaultCacheArchive,
        ResourceIdentifier::BorrowedLookup(), prefix.data(), prefix.size());
    const auto found = mResourceCache.find(alternate);
    if (found == mResourceCache.end()) return std::shared_ptr<IResource>();
    return GetCachedResource(found->second);
}

"""+source[start:]
