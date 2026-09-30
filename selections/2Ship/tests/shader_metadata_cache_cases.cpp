#include "fast3d/shader_metadata_cache.h"
#include <cstdio>
#include <cstdlib>
#include <type_traits>

static void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

int main() {
    typedef Shipyard::Fast3D::ShaderMetadataCache<16> Cache;
    static_assert(std::is_trivial<Cache>::value, "combiner metadata remains POD");
    Cache cache = {};
    int backendA, backendB, programA, programB;
    Shipyard::Fast3D::ShaderMetadata value = {}, observed = {};
    require(!cache.Lookup(3, &backendA, &programA, observed), "empty entry hit");
    value.numInputs = 5; value.usedTextures[0] = true; value.usedTextures[1] = false;
    cache.Store(3, &backendA, &programA, value);
    require(cache.Lookup(3, &backendA, &programA, observed), "stored entry missed");
    require(observed.numInputs == 5 && observed.usedTextures[0] && !observed.usedTextures[1], "value changed");
    require(!cache.Lookup(4, &backendA, &programA, observed), "variant aliased");
    require(!cache.Lookup(3, &backendB, &programA, observed), "backend aliased");
    require(!cache.Lookup(3, &backendA, &programB, observed), "program replacement aliased");
    require(!cache.Lookup(3, &backendA, 0, observed), "null program hit");
    cache.Invalidate();
    require(!cache.Lookup(3, &backendA, &programA, observed), "same-address reinit hit");
    value.numInputs = 7; value.usedTextures[0] = false; value.usedTextures[1] = true;
    cache.Store(3, &backendA, &programA, value);
    require(cache.Lookup(3, &backendA, &programA, observed), "refresh missed");
    require(observed.numInputs == 7 && !observed.usedTextures[0] && observed.usedTextures[1], "refresh stale");
    std::puts("PASS shader metadata cache key, value and invalidation");
}
