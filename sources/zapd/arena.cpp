// Small-block arena for extraction threads. ZAPD makes about twenty million small
// allocations per ROM, which the XDK heap serves slowly, so each extraction thread
// takes blocks of up to 512 bytes from its own 64 KB slabs that each hold one size
// class. Other threads and larger blocks use malloc. A slab goes back to a shared
// pool once its blocks are all freed, so one thread's large file does not hold memory
// the others could use. A block another thread frees is only counted. When extraction
// ends, free slabs are decommitted; slabs that still hold blocks, such as statics, stay.
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
const int kNone = -1;

char* gBase;
volatile LONG gThreads; // threads inside an extraction
CRITICAL_SECTION gSlabLock;
// Per slab. Only the owner thread touches the free list, use, live count and links.
unsigned short gSlabClass[kSlabs]; // zero for a pooled slab
bool gSlabCommitted[kSlabs];
DWORD gSlabOwner[kSlabs];
void* gSlabFree[kSlabs];
size_t gSlabUsed[kSlabs]; // bytes handed out from the slab's start
LONG gLive[kSlabs];       // the owner's allocations less its own frees
LONG gRemote[kSlabs];     // frees by any other thread
int gSlabPrev[kSlabs];
int gSlabNext[kSlabs];
bool gSlabListed[kSlabs];
size_t gNextSlab;
size_t gFreeSlabs[kSlabs]; // pooled slabs, committed or not
size_t gFreeSlabCount;
__declspec(thread) bool tInside;
__declspec(thread) int tCurrent[kClasses]; // slab that serves each class, or kNone
__declspec(thread) int tPartial[kClasses]; // other own slabs with freed blocks, or kNone

bool TakeSlab(size_t grains) {
    int taken = kNone;
    EnterCriticalSection(&gSlabLock);
    size_t slab = kSlabs;
    if (gFreeSlabCount)
        slab = gFreeSlabs[--gFreeSlabCount];
    else if (gNextSlab < kSlabs)
        slab = gNextSlab++;
    if (slab < kSlabs) {
        if (gSlabCommitted[slab] || VirtualAlloc(gBase + slab * kSlab, kSlab, MEM_COMMIT, PAGE_READWRITE)) {
            gSlabCommitted[slab] = true;
            gSlabClass[slab] = (unsigned short)grains;
            gSlabOwner[slab] = GetCurrentThreadId();
            gSlabFree[slab] = NULL;
            gSlabUsed[slab] = 0;
            gLive[slab] = gRemote[slab] = 0;
            gSlabListed[slab] = false;
            taken = (int)slab;
        } else {
            gFreeSlabs[gFreeSlabCount++] = slab;
        }
    }
    LeaveCriticalSection(&gSlabLock);
    tCurrent[grains] = taken;
    return taken != kNone;
}

void GiveSlab(size_t slab) {
    EnterCriticalSection(&gSlabLock);
    gSlabClass[slab] = 0;
    gSlabOwner[slab] = 0;
    gFreeSlabs[gFreeSlabCount++] = slab;
    LeaveCriticalSection(&gSlabLock);
}

void Unlist(size_t slab, size_t grains) {
    if (gSlabPrev[slab] != kNone)
        gSlabNext[gSlabPrev[slab]] = gSlabNext[slab];
    else
        tPartial[grains] = gSlabNext[slab];
    if (gSlabNext[slab] != kNone)
        gSlabPrev[gSlabNext[slab]] = gSlabPrev[slab];
    gSlabListed[slab] = false;
}

void* Allocate(size_t size) {
    const size_t grains = size ? (size + kGrain - 1) / kGrain : 1;
    const size_t bytes = grains * kGrain;
    for (;;) {
        const int slab = tCurrent[grains];
        if (slab != kNone) {
            void* block = gSlabFree[slab];
            if (block != NULL) {
                gSlabFree[slab] = *(void**)block;
                ++gLive[slab];
                return block;
            }
            if (gSlabUsed[slab] + bytes <= kSlab) {
                block = gBase + slab * kSlab + gSlabUsed[slab];
                gSlabUsed[slab] += bytes;
                ++gLive[slab];
                return block;
            }
        }
        if (tPartial[grains] != kNone) {
            const int next = tPartial[grains];
            Unlist(next, grains);
            tCurrent[grains] = next;
        } else if (!TakeSlab(grains)) {
            return NULL;
        }
    }
}

void Release(void* block) {
    if (gBase != NULL && (char*)block >= gBase && (char*)block < gBase + kRegion) {
        const size_t slab = ((char*)block - gBase) / kSlab;
        if (!tInside || gSlabOwner[slab] != GetCurrentThreadId()) {
            InterlockedIncrement(&gRemote[slab]);
            return;
        }
        const size_t grains = gSlabClass[slab];
        *(void**)block = gSlabFree[slab];
        gSlabFree[slab] = block;
        if (tCurrent[grains] == (int)slab) {
            --gLive[slab];
        } else if (--gLive[slab] == gRemote[slab]) {
            if (gSlabListed[slab])
                Unlist(slab, grains);
            GiveSlab(slab);
        } else if (!gSlabListed[slab]) {
            gSlabPrev[slab] = kNone;
            gSlabNext[slab] = tPartial[grains];
            if (tPartial[grains] != kNone)
                gSlabPrev[tPartial[grains]] = (int)slab;
            tPartial[grains] = (int)slab;
            gSlabListed[slab] = true;
        }
        return;
    }
    free(block);
}
} // namespace

// Helper threads join an extraction's arena with Enter and leave with Leave.
void ShipyardZapdArenaEnter() {
    if (gBase != NULL) {
        for (size_t grains = 0; grains < kClasses; ++grains)
            tCurrent[grains] = tPartial[grains] = kNone;
        tInside = true;
        InterlockedIncrement(&gThreads);
    }
}

// The calling thread joins the arena before any helper starts.
void ShipyardZapdArenaBegin() {
    if (gBase == NULL) {
        static bool lockReady;
        if (!lockReady)
            InitializeCriticalSection(&gSlabLock);
        lockReady = true;
        gBase = (char*)VirtualAlloc(NULL, kRegion, MEM_RESERVE, PAGE_READWRITE);
    }
    ShipyardZapdArenaEnter();
}

// Slabs that still hold blocks stay with this thread's id; later frees of them are counted.
void ShipyardZapdArenaLeave() {
    if (!tInside)
        return;
    for (size_t grains = 0; grains < kClasses; ++grains) {
        const int slab = tCurrent[grains];
        if (slab != kNone && gLive[slab] == gRemote[slab])
            GiveSlab(slab);
        while (tPartial[grains] != kNone)
            Unlist(tPartial[grains], grains);
    }
    tInside = false;
    InterlockedDecrement(&gThreads);
}

// Called once every helper has left.
void ShipyardZapdArenaEnd() {
    ShipyardZapdArenaLeave();
    for (size_t slab = 0; slab < gNextSlab; ++slab) {
        if (gSlabClass[slab] != 0 && gLive[slab] == gRemote[slab]) {
            gSlabClass[slab] = 0;
            gSlabOwner[slab] = 0;
            gFreeSlabs[gFreeSlabCount++] = slab;
        }
        if (gSlabClass[slab] == 0 && gSlabCommitted[slab]) {
            VirtualFree(gBase + slab * kSlab, kSlab, MEM_DECOMMIT);
            gSlabCommitted[slab] = false;
        }
    }
}

void* operator new(size_t size) {
    if (gThreads != 0 && size <= kLargest && tInside)
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
