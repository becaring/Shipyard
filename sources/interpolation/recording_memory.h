#pragma once

#include <memory>
#include <new>
#include <stddef.h>

namespace Shipyard { namespace Interpolation {

struct RecordingMemoryCounts {
    unsigned requests, hits, releases, cachedReleases;
    size_t retainedBytes, peakRetainedBytes;
};

namespace Detail {
enum { Quantum = 32, MaxBlock = 4096, BinCount = MaxBlock / Quantum,
       CacheLimit = 2 * 1024 * 1024 };
struct Block { Block* next; };
struct Cache {
    Block* bins[BinCount];
    size_t retainedBytes, peakRetainedBytes;
    unsigned requests, hits, releases, cachedReleases;
};
// The recorder runs on the game thread. No object outlives its original
// container; the cache keeps only storage after destruction.
static __declspec(thread) Cache cache;

inline size_t Rounded(size_t bytes) {
    return bytes ? (bytes + Quantum - 1) & ~(size_t(Quantum) - 1) : Quantum;
}
}

inline void TrimRecordingMemory() {
    Detail::Cache& cache = Detail::cache;
    for (unsigned i = 0; i < Detail::BinCount; ++i) {
        Detail::Block* block = cache.bins[i];
        cache.bins[i] = NULL;
        while (block) {
            Detail::Block* next = block->next;
            ::operator delete(block);
            block = next;
        }
    }
    cache.retainedBytes = 0;
}

inline void* RecordingAllocate(size_t bytes) {
    Detail::Cache& cache = Detail::cache;
    ++cache.requests;
    const size_t size = bytes <= Detail::MaxBlock ? Detail::Rounded(bytes) : bytes;
    if (size <= Detail::MaxBlock) {
        Detail::Block*& bin = cache.bins[size / Detail::Quantum - 1];
        if (bin) {
            Detail::Block* block = bin;
            bin = block->next;
            cache.retainedBytes -= size;
            ++cache.hits;
            return block;
        }
    }
    try { return ::operator new(size); }
    catch (const std::bad_alloc&) {
        // Cached blocks in other size classes must not hide usable heap space.
        TrimRecordingMemory();
        return ::operator new(size);
    }
}

inline void RecordingFree(void* pointer, size_t bytes) {
    if (!pointer) return;
    Detail::Cache& cache = Detail::cache;
    ++cache.releases;
    if (bytes <= Detail::MaxBlock) {
        const size_t size = Detail::Rounded(bytes);
        if (cache.retainedBytes + size <= Detail::CacheLimit) {
            Detail::Block*& bin = cache.bins[size / Detail::Quantum - 1];
            Detail::Block* block = static_cast<Detail::Block*>(pointer);
            block->next = bin;
            bin = block;
            cache.retainedBytes += size;
            ++cache.cachedReleases;
            if (cache.retainedBytes > cache.peakRetainedBytes)
                cache.peakRetainedBytes = cache.retainedBytes;
            return;
        }
    }
    ::operator delete(pointer);
}

inline void GetRecordingMemoryCounts(RecordingMemoryCounts* counts) {
    Detail::Cache& cache = Detail::cache;
    counts->requests = cache.requests;
    counts->hits = cache.hits;
    counts->releases = cache.releases;
    counts->cachedReleases = cache.cachedReleases;
    counts->retainedBytes = cache.retainedBytes;
    counts->peakRetainedBytes = cache.peakRetainedBytes;
    cache.requests = cache.hits = cache.releases = cache.cachedReleases = 0;
}

template<class T> struct RecordingAllocator : std::allocator<T> {
    typedef T* pointer;
    typedef size_t size_type;
    template<class U> struct rebind { typedef RecordingAllocator<U> other; };
    RecordingAllocator() {}
    template<class U> RecordingAllocator(const RecordingAllocator<U>&) {}
    pointer allocate(size_type count, const void* = NULL) {
        if (count > size_type(-1) / sizeof(T)) throw std::bad_alloc();
        return static_cast<pointer>(RecordingAllocate(count * sizeof(T)));
    }
    void deallocate(pointer pointer, size_type count) {
        RecordingFree(pointer, count * sizeof(T));
    }
};
template<class T, class U> bool operator==(const RecordingAllocator<T>&, const RecordingAllocator<U>&) { return true; }
template<class T, class U> bool operator!=(const RecordingAllocator<T>&, const RecordingAllocator<U>&) { return false; }

} }
