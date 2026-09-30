#pragma once
#include <xtl.h>
#include <d3dx9.h>
#include <stddef.h>
#include <stdint.h>
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
// Optional read-only pack of offline-compiled Fast3D programs (see
// shader_pack.h), read into memory once. Returns false when the file is absent
// or was built for another XDK.
bool ConfigureShaderPack(const char* path);
// Microcode for one packed program; the pointers stay valid until shutdown.
struct PackedShaderProgram {
    const DWORD* vertex;
    size_t vertexBytes;
    const DWORD* pixel;
    size_t pixelBytes;
    size_t floats;
};
bool FindPackedProgram(uint64_t shaderId0, uint32_t shaderId1, bool threePoint, bool srgb,
                       PackedShaderProgram* program);
// The program whose lowered source the caller regenerates to prove the pack
// matches this build, and that source's SHA-256.
bool GetShaderPackProbe(uint64_t* shaderId0, uint32_t* shaderId1, bool* threePoint, bool* srgb,
                        size_t* floats, unsigned char sourceDigest[32]);
bool ConfigureShaderCache(const ShaderCacheConfig& config);
ID3DXBuffer* LoadShaderCode(const std::string& source, const char* entry, const char* profile);
void StoreShaderCode(const std::string& source, const char* entry, const char* profile,
                     ID3DXBuffer* code);
void GetShaderCacheCounts(ShaderCacheCounts* counts);
bool WriteShaderCacheRecord(const std::string& record, bool truncate);
} // namespace Xenos
} // namespace Shipyard
