#pragma once

namespace Shipyard {
namespace Xenos {

// Programs retain immutable keys and remain owned by the renderer until shutdown.
// The caller already needs a live program to read its key; retain no extra pointer.
template<class Program, class Key, class Lookup, class Create>
Program* ResolveShaderVariant(Program* program, const Key& original, const Key& requested,
                              Lookup lookup, Create create) {
    if (original.shaderId0 == requested.shaderId0 && original.shaderId1 == requested.shaderId1 &&
        original.filteringMode == requested.filteringMode && original.srgbMode == requested.srgbMode)
        return program;
    Program* found = lookup(requested);
    return found ? found : create(requested);
}

} // namespace Xenos
} // namespace Shipyard
