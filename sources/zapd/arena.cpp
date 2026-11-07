// Small-block arena for the extraction thread. ZAPD makes about twenty million
// small allocations per ROM, which the XDK heap serves slowly, so while an
// extraction runs its thread takes blocks of up to 512 bytes from 64 KB slabs
// that each hold one size class. Other threads and larger blocks use malloc;
// a larger limit lets per-class free lists raise the memory peak.
// When extraction ends, empty slabs are decommitted for the next run; slabs that
// still hold blocks, such as function statics, stay committed.
#ifdef _XBOX
#include <xtl.h>
#else
#include <windows.h>
#endif
#include <stdlib.h>
#include <new>

namespace {
const size_t kRegion = 256u << 20;
const size_t kSlab = 64u << 10;
const size_t kSlabs = kRegion / kSlab;
const size_t kGrain = 16;
const size_t kLargest = 512;
const size_t kClasses = kLargest / kGrain + 1;

char* gBase;
volatile DWORD gOwner;
unsigned short gSlabClass[kSlabs]; // zero for a slab that is not committed
LONG gLive[kSlabs];
size_t gNextSlab;
size_t gFreeSlabs[kSlabs];
size_t gFreeSlabCount;
void* gFree[kClasses];
char* gCursor[kClasses];
char* gEnd[kClasses];

void* Allocate(size_t size) {
    const size_t grains = size ? (size + kGrain - 1) / kGrain : 1;
    void* block = gFree[grains];
    if (block != NULL) {
        gFree[grains] = *(void**)block;
    } else {
        const size_t bytes = grains * kGrain;
        if ((size_t)(gEnd[grains] - gCursor[grains]) < bytes) {
            size_t slab;
            if (gFreeSlabCount)
                slab = gFreeSlabs[--gFreeSlabCount];
            else if (gNextSlab < kSlabs)
                slab = gNextSlab++;
            else
                return NULL;
            char* base = gBase + slab * kSlab;
            if (!VirtualAlloc(base, kSlab, MEM_COMMIT, PAGE_READWRITE)) {
                gFreeSlabs[gFreeSlabCount++] = slab;
                return NULL;
            }
            gSlabClass[slab] = (unsigned short)grains;
            gCursor[grains] = base;
            gEnd[grains] = base + kSlab;
        }
        block = gCursor[grains];
        gCursor[grains] += bytes;
    }
    ++gLive[((char*)block - gBase) / kSlab];
    return block;
}

void Release(void* block) {
    if (gBase != NULL && (char*)block >= gBase && (char*)block < gBase + kRegion) {
        const size_t slab = ((char*)block - gBase) / kSlab;
        if (gOwner == GetCurrentThreadId()) {
            --gLive[slab];
            *(void**)block = gFree[gSlabClass[slab]];
            gFree[gSlabClass[slab]] = block;
        } else {
            InterlockedDecrement(&gLive[slab]);
        }
        return;
    }
    free(block);
}
} // namespace

void ShipyardZapdArenaBegin() {
    if (gBase == NULL)
        gBase = (char*)VirtualAlloc(NULL, kRegion, MEM_RESERVE, PAGE_READWRITE);
    if (gBase != NULL)
        gOwner = GetCurrentThreadId();
}

void ShipyardZapdArenaEnd() {
    gOwner = 0;
    for (size_t slab = 0; slab < gNextSlab; ++slab)
        if (gSlabClass[slab] != 0 && gLive[slab] == 0) {
            VirtualFree(gBase + slab * kSlab, kSlab, MEM_DECOMMIT);
            gSlabClass[slab] = 0;
            gFreeSlabs[gFreeSlabCount++] = slab;
        }
    for (size_t grains = 0; grains < kClasses; ++grains) {
        gFree[grains] = NULL;
        gCursor[grains] = gEnd[grains] = NULL;
    }
}

void* operator new(size_t size) {
    if (gOwner != 0 && size <= kLargest && gOwner == GetCurrentThreadId())
        if (void* block = Allocate(size))
            return block;
    void* block = malloc(size ? size : 1);
    if (block == NULL)
        throw std::bad_alloc();
    return block;
}

void* operator new[](size_t size) { return operator new(size); }
void operator delete(void* block) throw() { Release(block); }
void operator delete[](void* block) throw() { Release(block); }
#if defined(_MSC_VER) && _MSC_VER >= 1900
void operator delete(void* block, size_t) noexcept { Release(block); }
void operator delete[](void* block, size_t) noexcept { Release(block); }
#endif
