#include "services.h"
#include "shader_cache.h"
#include "shader_pack.h"
#include <xdk.h>
#include <stdio.h>
#include <string.h>
#include <map>
#include <new>
#include <utility>
#include <vector>
#include <algorithm>

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

// Read-only prebuilt pack, resident after one sequential read at startup: a
// hit costs a binary search, never file I/O on the render thread. Lookups
// happen on the render thread only.
struct PrebuiltPack {
    std::vector<DWORD> words; // whole file; DWORD storage keeps microcode aligned
    std::vector<ShaderPackProgram> index;
    std::vector<unsigned char> checked; // 0 unchecked, 1 good, 2 corrupt
    uint32_t probe;
    unsigned char probeDigest[32];
    PrebuiltPack() : probe(0) {}
    const unsigned char* Bytes() const {
        return reinterpret_cast<const unsigned char*>(&words[0]);
    }
    bool Open(const char* path) {
        if (!index.empty() || !path)
            return false;
        FILE* handle = fopen(path, "rb");
        if (!handle)
            return false;
        bool valid = fseek(handle, 0, SEEK_END) == 0;
        const long length = valid ? ftell(handle) : -1;
        valid = valid && length >= (long)ShaderPackHeaderBytes && !(length & 3) &&
                fseek(handle, 0, SEEK_SET) == 0;
        std::vector<DWORD> file;
        std::vector<ShaderPackProgram> entries;
        try {
            if (valid) {
                file.resize((size_t)length / 4);
                valid = fread(&file[0], 1, (size_t)length, handle) == (size_t)length;
            }
            const unsigned char* bytes = valid ? reinterpret_cast<const unsigned char*>(&file[0]) : NULL;
            valid = valid && ShaderPackRead32(bytes) == ShaderPackMagic &&
                    ShaderPackRead32(bytes + 4) == ShaderPackVersion &&
                    ShaderPackRead32(bytes + 8) == _XDK_VER &&
                    ShaderPackRead32(bytes + 12) == D3DX_SDK_VERSION;
            const uint32_t count = valid ? ShaderPackRead32(bytes + 16) : 0;
            probe = valid ? ShaderPackRead32(bytes + 20) : 0;
            valid = valid && count && count <= ShaderPackMaxEntries && probe < count &&
                    ShaderPackHeaderBytes + (size_t)count * ShaderPackEntryBytes <= (size_t)length;
            entries.reserve(count);
            for (uint32_t i = 0; valid && i < count; ++i) {
                const ShaderPackProgram entry =
                    ShaderPackParseProgram(bytes + ShaderPackHeaderBytes + i * ShaderPackEntryBytes);
                valid = Within(entry.vertexOffset, entry.vertexSize, (size_t)length) &&
                        Within(entry.pixelOffset, entry.pixelSize, (size_t)length) &&
                        entry.floats && entry.flags <= (ShaderPackThreePoint | ShaderPackSrgb) &&
                        (!i || ShaderPackBefore(entries[i - 1].shaderId0, entries[i - 1].shaderId1,
                                                entries[i - 1].flags, entry.shaderId0,
                                                entry.shaderId1, entry.flags));
                entries.push_back(entry);
            }
            if (valid) {
                checked.assign(count, 0);
                memcpy(probeDigest, bytes + 32, sizeof(probeDigest));
            }
        } catch (const std::bad_alloc&) {
            valid = false;
        }
        fclose(handle);
        if (!valid) {
            Shipyard_Log("shader pack rejected: truncated, unsorted or built for another XDK");
            return false;
        }
        words.swap(file);
        index.swap(entries);
        Shipyard_Log("shader pack loaded programs=%u bytes=%u", (unsigned)index.size(),
                     (unsigned)(words.size() * 4));
        return true;
    }
    static bool Within(uint32_t offset, uint32_t size, size_t length) {
        return size && size <= MaxCode && !(size & 3) && !(offset & 3) &&
               offset >= ShaderPackHeaderBytes && offset <= length && size <= length - offset;
    }
    static bool Less(const ShaderPackProgram& entry, const ShaderPackProgram& key) {
        return ShaderPackBefore(entry.shaderId0, entry.shaderId1, entry.flags, key.shaderId0,
                                key.shaderId1, key.flags);
    }
    // The checksum covers the key, floats and both microcode blobs; each entry
    // is verified on its first use.
    bool Intact(size_t i) {
        if (!checked[i]) {
            const ShaderPackProgram& entry = index[i];
            const unsigned char* bytes = Bytes();
            unsigned char head[20];
            for (int b = 0; b < 8; ++b)
                head[b] = (unsigned char)(entry.shaderId0 >> (56 - 8 * b));
            const uint32_t fields[3] = {entry.shaderId1, entry.flags, entry.floats};
            for (int f = 0; f < 3; ++f)
                for (int b = 0; b < 4; ++b)
                    head[8 + f * 4 + b] = (unsigned char)(fields[f] >> (24 - 8 * b));
            uint32_t crc = ShaderPackChecksum(head, sizeof(head));
            crc = ShaderPackChecksum(bytes + entry.vertexOffset, entry.vertexSize, crc);
            crc = ShaderPackChecksum(bytes + entry.pixelOffset, entry.pixelSize, crc);
            checked[i] = crc == entry.checksum ? 1 : 2;
        }
        return checked[i] == 1;
    }
    bool Find(uint64_t shaderId0, uint32_t shaderId1, uint32_t flags, PackedShaderProgram* out) {
        if (index.empty() || !out)
            return false;
        ShaderPackProgram key = {};
        key.shaderId0 = shaderId0;
        key.shaderId1 = shaderId1;
        key.flags = flags;
        std::vector<ShaderPackProgram>::const_iterator it =
            std::lower_bound(index.begin(), index.end(), key, Less);
        if (it == index.end() || Less(key, *it) || !Intact(it - index.begin()))
            return false;
        const unsigned char* bytes = Bytes();
        out->vertex = reinterpret_cast<const DWORD*>(bytes + it->vertexOffset);
        out->vertexBytes = it->vertexSize;
        out->pixel = reinterpret_cast<const DWORD*>(bytes + it->pixelOffset);
        out->pixelBytes = it->pixelSize;
        out->floats = it->floats;
        return true;
    }
} pack;

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

bool ConfigureShaderPack(const char* path) {
    return pack.Open(path);
}
bool FindPackedProgram(uint64_t shaderId0, uint32_t shaderId1, bool threePoint, bool srgb,
                       PackedShaderProgram* program) {
    return pack.Find(shaderId0, shaderId1, ShaderPackFlags(threePoint, srgb), program);
}
bool GetShaderPackProbe(uint64_t* shaderId0, uint32_t* shaderId1, bool* threePoint, bool* srgb,
                        size_t* floats, unsigned char sourceDigest[32]) {
    if (pack.index.empty())
        return false;
    const ShaderPackProgram& entry = pack.index[pack.probe];
    *shaderId0 = entry.shaderId0;
    *shaderId1 = entry.shaderId1;
    *threePoint = (entry.flags & ShaderPackThreePoint) != 0;
    *srgb = (entry.flags & ShaderPackSrgb) != 0;
    *floats = entry.floats;
    memcpy(sourceDigest, pack.probeDigest, sizeof(pack.probeDigest));
    return true;
}
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
    try {
        // Programs the pack covers never reach source compilation; this cache
        // holds the runtime misses.
        const std::string key = Key(source, entry, profile);
        if (!cache.configured)
            return NULL;
        const std::vector<DWORD>* code = cache.Find(key);
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
