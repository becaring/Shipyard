"""Libultraship resource-cache and archive concurrency shared by every consumer.

Resource workers, render and the overlapped game worker all load resources.
Every cache map access takes mMutex, the first published instance of a path
wins (game code keeps raw pointers into cached resources, so replacing an entry
would free an instance it still reads), and an erased entry is destroyed after
the lock is released because resource destructors may load other resources.
Archive lookups never insert, and libzip handles are never shared by threads.
"""

import re


def _once(source, old, new):
    if source.count(old) != 1:
        raise ValueError('LUS resource anchor changed: ' + old[:80])
    return source.replace(old, new, 1)


def _subn_once(pattern, replacement, source):
    source, count = re.subn(pattern, replacement, source)
    if count != 1:
        raise ValueError('LUS resource anchor changed: ' + pattern[:80])
    return source


def _lock(source):
    found = re.search(r'const ([\w:]+::lock_guard<[\w:]+::mutex>) lock\(mMutex\);', source)
    if not found:
        raise ValueError('LUS resource cache lock spelling changed')
    return found.group(1)


INCREMENT = r'(?:        InterlockedIncrement\(&mXboxLookupGeneration\);\n)?'


def concurrent_cache(source):
    """Lock the missing-file NotFound write, keep the first published instance,
    and erase under the lock while destroying after it.

    Accepts the upstream text, the console lowering of contains() and the
    optional lookup-generation increments, so it can run before or after
    resource_lookup.lookup_generation_source.
    """
    lock = _lock(source)
    source = _subn_once(
        r'        mResourceCache\[identifier\] = ResourceLoadError::NotFound;\n(' + INCREMENT + r')        return nullptr;',
        lambda m: '        {\n            const ' + lock + ' lock(mMutex);\n'
                  '            mResourceCache[identifier] = ResourceLoadError::NotFound;\n        }\n'
                  + m.group(1) + '        return nullptr;', source)
    source = _once(source,
        '        if (cachedResource != nullptr) {\n'
        '            // If another thread has already loaded this resource, discard the work we already did and return from\n'
        '            // cache.\n',
        '        const auto published = mResourceCache.find(identifier);\n'
        '        if (published != mResourceCache.end()) {\n'
        '            const std::shared_ptr<IResource> current = GetCachedResource(published->second);\n'
        '            if (current != nullptr) {\n'
        '                cachedResource = current;\n'
        '            }\n'
        '        }\n'
        '        if (cachedResource != nullptr) {\n'
        '            // If another thread has already loaded this resource, discard the work we already did and return from\n'
        '            // cache.\n')
    contains = (r'(?:mResourceCache\.contains\(identifier\)|mResourceCache\.find\(identifier\) != mResourceCache\.end\(\)'
                r'|\(mResourceCache\.find\(identifier\) != mResourceCache\.end\(\)\))')
    return _subn_once(
        r'    if \(' + contains + r'\) \{\n        const [\w:<>]+ lock\(mMutex\);\n'
        r'        mResourceCache\.erase\(identifier\);\n(' + INCREMENT + r')    \}',
        lambda m: '    {\n        const ' + lock + ' lock(mMutex);\n'
                  '        const auto found = mResourceCache.find(identifier);\n'
                  '        if (found != mResourceCache.end()) {\n'
                  '            value = found->second;\n'
                  '            mResourceCache.erase(found);\n'
                  '            ret = 1;\n'
                  + m.group(1).replace('        ', '            ', 1) +
                  '        }\n    }', source)


def archive_find(source):
    """Missing-hash lookups return null without inserting into mFileToArchive."""
    source = _once(source, '    auto archive = mFileToArchive[hash];\n',
        '    std::shared_ptr<Archive> archive;\n'
        '    const auto found = mFileToArchive.find(hash);\n'
        '    if (found != mFileToArchive.end()) {\n'
        '        archive = found->second;\n'
        '    }\n')
    return _once(source, '    return mFileToArchive[CRC64(filePath.c_str())];',
        '    const auto found = mFileToArchive.find(CRC64(filePath.c_str()));\n'
        '    if (found == mFileToArchive.end()) {\n'
        '        return std::shared_ptr<Archive>();\n'
        '    }\n'
        '    return found->second;')


def archive_rehash(source):
    """Size both indices once per added archive instead of rehashing while inserting.

    VC10's unordered_map has rehash() but no reserve().
    """
    anchor = '    const auto fileList = archive->ListFiles();\n'
    return _once(source, anchor, anchor +
        '    mHashes.rehash(static_cast<size_t>((mHashes.size() + fileList->size()) /\n'
        '                                       mHashes.max_load_factor()) + 1);\n'
        '    mFileToArchive.rehash(static_cast<size_t>((mFileToArchive.size() + fileList->size()) /\n'
        '                                              mFileToArchive.max_load_factor()) + 1);\n')


READ = ('    if (zip_fread(zipEntryFile, fileToLoad->Buffer->data(), zipEntryStat.size) < 0) {\n'
        '        SPDLOG_TRACE("Error reading file {} in zip archive  {}.", filePath, GetPath());\n'
        '    }\n')
CLOSE = ('\n    if (zip_fclose(zipEntryFile) != 0) {\n'
         '        SPDLOG_TRACE("Error closing file {} in zip archive  {}.", filePath, GetPath());\n'
         '    }\n\n    ReleaseZipHandle(zipArchive);')


def o2r_partial_read(source, *, handle_pool):
    """Complete every declared entry across positive partial chunks; a failed read is not a file.

    A pooled handle (zipArchive) whose read failed is discarded, not returned to the pool.
    """
    if not handle_pool:
        return _once(source, READ,
            '    zip_uint64_t bytesRead = 0;\n'
            '    while (bytesRead < zipEntryStat.size) {\n'
            '        const zip_uint64_t remaining = zipEntryStat.size - bytesRead;\n'
            '        const zip_int64_t chunk = zip_fread(zipEntryFile, fileToLoad->Buffer->data() +\n'
            '                                            static_cast<size_t>(bytesRead), remaining);\n'
            '        if (chunk <= 0 || static_cast<zip_uint64_t>(chunk) > remaining) {\n'
            '            SPDLOG_TRACE("Error reading file {} in zip archive  {}.", filePath, GetPath());\n'
            '            zip_fclose(zipEntryFile);\n'
            '            return nullptr;\n'
            '        }\n'
            '        bytesRead += static_cast<zip_uint64_t>(chunk);\n'
            '    }\n')
    return _once(source, READ + CLOSE,
        '    zip_uint64_t readTotal = 0;\n'
        '    while (readTotal < zipEntryStat.size) {\n'
        '        zip_int64_t readCount = zip_fread(zipEntryFile, fileToLoad->Buffer->data() + readTotal,'
        ' zipEntryStat.size - readTotal);\n'
        '        if (readCount <= 0) {\n'
        '            zip_fclose(zipEntryFile);\n'
        '            zip_discard(zipArchive);\n'
        '            return nullptr;\n'
        '        }\n'
        '        readTotal += static_cast<zip_uint64_t>(readCount);\n'
        '    }\n'
        '    if (zip_fclose(zipEntryFile) != 0) {\n'
        '        zip_discard(zipArchive);\n'
        '        return nullptr;\n'
        '    }\n'
        '    ReleaseZipHandle(zipArchive);')


def o2r_zip_mutex(source):
    """Serialize a single shared zip_t; libzip supports one thread per handle.

    Without it, inline misses on render and the game worker beside resource
    workers interleave reads, return failed or foreign entry data, and the
    cache records the path as NotFound. Use only on LUS without a handle pool.
    """
    if 'mZipArchivePool' in source:
        raise ValueError('O2R archive has a handle pool; the zip mutex is for single-handle LUS')
    source = _once(source, 'namespace Ship {\nO2rArchive::O2rArchive(',
        'namespace Ship {\nnamespace {\n// Namespace scope: constructed before any resource worker starts.\n'
        'Shipyard::Async::mutex sXboxZipMutex;\n}\n\nO2rArchive::O2rArchive(')
    for signature in ('std::shared_ptr<File> O2rArchive::LoadFile(const std::string& filePath) {\n',
                      'bool O2rArchive::WriteFile(const std::string& filePath, const std::vector<uint8_t>& data) {\n'):
        source = _once(source, signature, signature +
            '    const Shipyard::Async::lock_guard<Shipyard::Async::mutex> zipLock(sXboxZipMutex);\n')
    return '#include "async/native.h"\n' + source
