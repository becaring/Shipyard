#pragma once
#include <cstring>
#include <string>
#include "path_hash.h"

namespace Shipyard { namespace Resources {
// Synchronous lookup view: retention explicitly materializes Owned().
struct PathSpan {
    const char* first;
    size_t firstSize;
    const char* second;
    size_t secondSize;
    PathSpan(const char* a, size_t an, const char* b = "", size_t bn = 0)
        : first(a), firstSize(an), second(b), secondSize(bn) {}
    size_t Size() const { return firstSize + secondSize; }
    std::string Owned() const { std::string result(first, firstSize); result.append(second, secondSize); return result; }
    size_t Hash() const { return PathHash::Append(PathHash::Append(2166136261u, first, firstSize), second, secondSize); }
    bool operator==(const PathSpan& rhs) const {
        if (Size() != rhs.Size()) return false;
        size_t offset = 0;
        while (offset < Size()) {
            const bool aFirst = offset < firstSize;
            const bool bFirst = offset < rhs.firstSize;
            const size_t aRemaining = aFirst ? firstSize - offset : Size() - offset;
            const size_t bRemaining = bFirst ? rhs.firstSize - offset : rhs.Size() - offset;
            const size_t count = aRemaining < bRemaining ? aRemaining : bRemaining;
            const char* a = aFirst ? first + offset : second + offset - firstSize;
            const char* b = bFirst ? rhs.first + offset : rhs.second + offset - rhs.firstSize;
            if (std::memcmp(a, b, count) != 0) return false;
            offset += count;
        }
        return true;
    }
};
}}
