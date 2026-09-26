"""Keep complete asset-path distinctions in the in-memory resource cache."""


def _once(source, old, new):
    if source.count(old) != 1:
        raise ValueError('resource lookup source anchor changed: ' + old[:70])
    return source.replace(old, new, 1)


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


def borrowed_cache_source(source):
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
