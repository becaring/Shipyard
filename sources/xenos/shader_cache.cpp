#include "services.h"
#include "shader_cache.h"
#include <xdk.h>
#include <stdio.h>
#include <map>
#include <new>
#include <utility>
#include <vector>

namespace Shipyard {
namespace Xenos {
namespace {
const DWORD Magic = 0x53484348;
const size_t Limit = 16 * 1024 * 1024;
const size_t MaxKey = 64 * 1024;
const size_t MaxCode = 256 * 1024;
const size_t MaxRecords = 2048;
DWORD Checksum(const void* bytes, size_t size, DWORD hash = 2166136261U) {
    const unsigned char* data = static_cast<const unsigned char*>(bytes);
    while (size--)
        hash = (hash ^ *data++) * 16777619U;
    return hash;
}
std::string Key(const std::string& source, const char* entry, const char* profile) {
    std::string key(profile);
    key += '\0';
    key += entry;
    key += '\0';
    key += source;
    return key;
}
struct CodeCache {
    typedef std::map<std::string, std::vector<DWORD> > Entries;
    Entries entries;
    std::string path;
    size_t bytes;
    bool configured;
    bool loaded;
    bool appendable;
    bool writable;
    unsigned version;
    unsigned hits;
    unsigned misses;
    unsigned compiled;
    unsigned writes;
    bool (*queueWrite)(const std::string&, bool, void (*)(bool));
    CodeCache()
        : bytes(5 * sizeof(DWORD)), configured(false), loaded(false), appendable(false),
          writable(true), version(0), hits(0), misses(0), compiled(0), writes(0), queueWrite(NULL) {
    }
    void Header(DWORD out[5]) const {
        out[0] = Magic;
        out[1] = version;
        out[2] = _XDK_VER;
        out[3] = D3DX_SDK_VERSION;
        out[4] = 0;
    }
    void Load() {
        if (loaded || !configured)
            return;
        loaded = true;
        FILE* file = fopen(path.c_str(), "rb");
        if (!file)
            return;
        bool valid = fseek(file, 0, SEEK_END) == 0;
        long length = valid ? ftell(file) : -1;
        valid = valid && length >= (long)(5 * sizeof(DWORD)) && length <= (long)Limit &&
                fseek(file, 0, SEEK_SET) == 0;
        DWORD header[5];
        DWORD expected[5];
        Header(expected);
        if (valid)
            valid = fread(header, 1, sizeof(header), file) == sizeof(header) &&
                    !memcmp(header, expected, sizeof(header));
        size_t remaining = valid ? (size_t)length - sizeof(header) : 0;
        try {
            while (valid && remaining) {
                DWORD record[3];
                valid = remaining >= sizeof(record) && entries.size() < MaxRecords &&
                        fread(record, 1, sizeof(record), file) == sizeof(record);
                if (!valid)
                    break;
                remaining -= sizeof(record);
                const size_t keySize = record[0];
                const size_t codeSize = record[1];
                valid = keySize && keySize <= MaxKey && codeSize && codeSize <= MaxCode &&
                        !(codeSize & 3) && keySize + codeSize <= remaining;
                if (!valid)
                    break;
                std::string key(keySize, '\0');
                std::vector<DWORD> code(codeSize / 4);
                valid = fread(&key[0], 1, keySize, file) == keySize &&
                        fread(&code[0], 1, codeSize, file) == codeSize;
                valid = valid &&
                        Checksum(&code[0], codeSize, Checksum(key.data(), keySize)) == record[2];
                if (valid)
                    entries.insert(std::make_pair(std::move(key), std::move(code)));
                remaining -= keySize + codeSize;
            }
        } catch (const std::bad_alloc&) {
            valid = false;
        }
        fclose(file);
        if (valid) {
            bytes = (size_t)length;
            appendable = true;
            Shipyard_Log("shader cache loaded entries=%u bytes=%u", (unsigned)entries.size(),
                         (unsigned)bytes);
        } else {
            entries.clear();
            bytes = 5 * sizeof(DWORD);
            Shipyard_Log(
                "shader cache rejected: truncated, corrupt or incompatible; compiling normally");
        }
    }
    const std::vector<DWORD>* Find(const std::string& key) {
        Load();
        Entries::const_iterator it = entries.find(key);
        if (it == entries.end()) {
            ++misses;
            return NULL;
        }
        ++hits;
        return &it->second;
    }
    void Store(const std::string& key, const void* code, size_t size);
} cache;

void CacheCompleted(bool ok) {
    if (!ok && cache.writable) {
        cache.writable = false;
        try {
            Shipyard_Log("shader cache persistence failed; runtime compilation remains available");
        } catch (...) {
            // Cache diagnostics cannot stop rendering.
        }
    }
}
void CodeCache::Store(const std::string& key, const void* code, size_t size) {
    Load();
    ++compiled;
    if (!configured || !size || (size & 3) || size > MaxCode || key.empty() ||
        key.size() > MaxKey || entries.size() >= MaxRecords ||
        bytes + sizeof(DWORD) * 3 + key.size() + size > Limit || entries.find(key) != entries.end())
        return;
    try {
        std::vector<DWORD> owned(size / 4);
        memcpy(&owned[0], code, size);
        entries.insert(std::make_pair(key, std::move(owned)));
    } catch (const std::bad_alloc&) {
        return;
    }
    bytes += sizeof(DWORD) * 3 + key.size() + size;
    if (!writable)
        return;
    DWORD record[] = {(DWORD)key.size(), (DWORD)size,
                      Checksum(code, size, Checksum(key.data(), key.size()))};
    if (queueWrite) {
        DWORD header[5];
        Header(header);
        std::string payload;
        if (!appendable)
            payload.append((const char*)header, sizeof(header));
        payload.append((const char*)record, sizeof(record));
        payload.append(key);
        payload.append((const char*)code, size);
        if (queueWrite(payload, !appendable, CacheCompleted)) {
            appendable = true;
            ++writes;
        } else {
            writable = false;
            Shipyard_Log("shader cache queue unavailable; runtime compilation remains available");
        }
        return;
    }
    FILE* file = fopen(path.c_str(), appendable ? "ab" : "wb");
    bool ok = file != NULL;
    if (ok && !appendable) {
        DWORD header[5];
        Header(header);
        ok = fwrite(header, 1, sizeof(header), file) == sizeof(header);
    }
    if (ok)
        ok = fwrite(record, 1, sizeof(record), file) == sizeof(record) &&
             fwrite(key.data(), 1, key.size(), file) == key.size() &&
             fwrite(code, 1, size, file) == size;
    if (file && fclose(file) != 0)
        ok = false;
    if (ok) {
        appendable = true;
        ++writes;
    } else {
        writable = false;
        Shipyard_Log("shader cache write unavailable; runtime compilation remains available");
    }
}
} // namespace

bool ConfigureShaderCache(const ShaderCacheConfig& config) {
    if (cache.loaded || cache.configured || !config.path || !config.path[0] ||
        !config.formatVersion)
        return false;
    cache.path = config.path;
    cache.version = config.formatVersion;
    cache.queueWrite = config.queueWrite;
    cache.configured = true;
    return true;
}
ID3DXBuffer* LoadShaderCode(const std::string& source, const char* entry, const char* profile) {
    if (!cache.configured)
        return NULL;
    try {
        const std::vector<DWORD>* code = cache.Find(Key(source, entry, profile));
        if (!code)
            return NULL;
        ID3DXBuffer* buffer = NULL;
        if (FAILED(D3DXCreateBuffer((DWORD)(code->size() * 4), &buffer)))
            return NULL;
        memcpy(buffer->GetBufferPointer(), &(*code)[0], code->size() * 4);
        return buffer;
    } catch (...) {
        // Optional lookup falls back to compilation.
        return NULL;
    }
}
void StoreShaderCode(const std::string& source, const char* entry, const char* profile,
                     ID3DXBuffer* code) {
    if (!cache.configured || !code)
        return;
    try {
        cache.Store(Key(source, entry, profile), code->GetBufferPointer(), code->GetBufferSize());
    } catch (...) {
        // Keep the successfully compiled shader.
        CacheCompleted(false);
    }
}
void GetShaderCacheCounts(ShaderCacheCounts* counts) {
    if (!counts)
        return;
    counts->entries = (unsigned)cache.entries.size();
    counts->hits = cache.hits;
    counts->misses = cache.misses;
    counts->compiled = cache.compiled;
    counts->writes = cache.writes;
    counts->bytes = (unsigned)cache.bytes;
}
bool WriteShaderCacheRecord(const std::string& record, bool truncate) {
    if (!cache.configured)
        return false;
    static bool failed;
    if (truncate)
        failed = false;
    if (failed)
        return false;
    FILE* file = fopen(cache.path.c_str(), truncate ? "wb" : "ab");
    bool ok = file && fwrite(record.data(), 1, record.size(), file) == record.size();
    if (file && fclose(file) != 0)
        ok = false;
    if (!ok)
        failed = true;
    return ok;
}
} // namespace Xenos
} // namespace Shipyard
