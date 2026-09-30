#include <cassert>
#include <cstdlib>
#include <new>
#include <vector>
static bool failNext;
static unsigned attempts;
void* operator new(size_t size) {
    ++attempts;
    if(failNext) { failNext=false; throw std::bad_alloc(); }
    if(void* p=std::malloc(size))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,size_t) noexcept { std::free(p); }
#include "interpolation/recording_memory.h"
int main() {
    using namespace Shipyard::Interpolation;
    RecordingAllocator<unsigned char> allocator;
    std::vector<unsigned char*> blocks;
    for(unsigned i=0;i<600;++i)blocks.push_back(allocator.allocate(4096));
    for(auto* p:blocks)allocator.deallocate(p,4096);
    RecordingMemoryCounts counts;
    GetRecordingMemoryCounts(&counts);
    assert(counts.retainedBytes==2*1024*1024);
    unsigned before=attempts;
    auto* reused=allocator.allocate(4096);
    assert(attempts==before);
    allocator.deallocate(reused,4096);
    // The requested bin is empty: failed heap allocation must trim other bins
    // before one retry, rather than holding reusable memory through failure.
    failNext=true; before=attempts;
    auto* retried=allocator.allocate(64);
    assert(attempts==before+2);
    GetRecordingMemoryCounts(&counts);assert(counts.retainedBytes==0);
    allocator.deallocate(retried,64);
    TrimRecordingMemory();GetRecordingMemoryCounts(&counts);assert(counts.retainedBytes==0);
    RecordingAllocator<unsigned long long> wide;
    bool rejected=false;
    try { wide.allocate(size_t(-1)); } catch(const std::bad_alloc&) { rejected=true; }
    assert(rejected);
}
