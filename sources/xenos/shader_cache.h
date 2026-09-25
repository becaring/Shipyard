#pragma once
#include <xtl.h>
#include <d3dx9.h>
#include <string>

namespace Shipyard {
namespace Xenos {
struct ShaderCacheConfig {
    const char* path;
    unsigned formatVersion;
    bool (*queueWrite)(const std::string&, bool, void (*)(bool));
};
struct ShaderCacheCounts {
    unsigned entries, hits, misses, compiled, writes, bytes;
};
bool ConfigureShaderCache(const ShaderCacheConfig& config);
ID3DXBuffer* LoadShaderCode(const std::string& source, const char* entry, const char* profile);
void StoreShaderCode(const std::string& source, const char* entry, const char* profile,
                     ID3DXBuffer* code);
void GetShaderCacheCounts(ShaderCacheCounts* counts);
bool WriteShaderCacheRecord(const std::string& record, bool truncate);
} // namespace Xenos
} // namespace Shipyard
