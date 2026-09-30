#include "xenos/shader_cache.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

static bool fail_next_allocation;
static size_t fail_min_allocation;
void* operator new(size_t size) {
    if (fail_next_allocation && size >= fail_min_allocation) {
        fail_next_allocation = false;
        throw std::bad_alloc();
    }
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

class Buffer : public ID3DXBuffer {
public:
    explicit Buffer(DWORD size) : bytes(size) {}
    void* GetBufferPointer() { return bytes.data(); }
    DWORD GetBufferSize() { return static_cast<DWORD>(bytes.size()); }
    ULONG Release() { delete this; return 0; }
private:
    std::vector<unsigned char> bytes;
};
HRESULT D3DXCreateBuffer(DWORD size, ID3DXBuffer** output) {
    try { *output = new Buffer(size); return S_OK; }
    catch (...) { *output = NULL; return E_OUTOFMEMORY; }
}
static bool ThrowQueueWrite(const std::string&, bool, void (*)(bool)) {
    throw std::bad_alloc();
}
static unsigned queue_calls;
static bool CountQueueWrite(const std::string&, bool, void (*)(bool)) {
    ++queue_calls;
    return true;
}
int main(int argc, char** argv) {
    using namespace Shipyard::Xenos;
    const bool payload_case = argc > 1 && std::strcmp(argv[1], "payload") == 0;
    const ShaderCacheConfig config = {"nonexistent-cache-file", 1,
        payload_case ? CountQueueWrite : ThrowQueueWrite};
    if (!ConfigureShaderCache(config)) return 1;
    Buffer compiled(4);
    std::memcpy(compiled.GetBufferPointer(), "CODE", 4);
    if (payload_case) {
        const std::string source(64, 'x');
        fail_min_allocation = 110;
        fail_next_allocation = true;
        try { StoreShaderCode(source, "main", "ps_3_0", &compiled); }
        catch (...) { return 10; }
        ShaderCacheCounts state = {};
        GetShaderCacheCounts(&state);
        if (fail_next_allocation || queue_calls || state.entries != 1 ||
            state.compiled != 1 || state.writes != 0 ||
            std::memcmp(compiled.GetBufferPointer(), "CODE", 4)) return 11;
        std::puts("shader cache optional payload allocation failure PASS");
        return 0;
    }
    try { StoreShaderCode("one", "main", "ps_3_0", &compiled); }
    catch (...) { return 2; }
    if (std::memcmp(compiled.GetBufferPointer(), "CODE", 4)) return 3;
    ShaderCacheCounts counts = {};
    GetShaderCacheCounts(&counts);
    if (counts.compiled != 1 || counts.entries != 1 || counts.writes != 0)
        return 4;
    ID3DXBuffer* cached = LoadShaderCode("one", "main", "ps_3_0");
    if (!cached || std::memcmp(cached->GetBufferPointer(), "CODE", 4))
        return 5;
    cached->Release();
    const std::string long_source(256, 'x');
    fail_next_allocation = true;
    try { StoreShaderCode(long_source, "main", "ps_3_0", &compiled); }
    catch (...) { return 6; }
    if (fail_next_allocation) return 7;
    fail_next_allocation = true;
    try { cached = LoadShaderCode(long_source, "main", "ps_3_0"); }
    catch (...) { return 8; }
    if (cached || fail_next_allocation) return 9;
    std::puts("shader cache optional allocation failure PASS");
}
