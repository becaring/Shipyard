#pragma once

#include <new>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#define SHIPYARD_BUFFER_INLINE __forceinline
#define SHIPYARD_BUFFER_COLD __declspec(noinline)
#else
#define SHIPYARD_BUFFER_INLINE inline
#define SHIPYARD_BUFFER_COLD
#endif

namespace Shipyard { namespace Fast3D {

// One owner writes POD records directly into retained storage. Append does
// not publish a logical element until the caller has assigned every field.
// Growth preserves record indices; a pointer returned for the current append
// must not be retained across another append that can grow the allocation.
template<class Record, unsigned Limit>
class InstructionBuffer {
public:
    InstructionBuffer() : mRecords(0), mSize(0), mCapacity(0) {}
    ~InstructionBuffer() { free(mRecords); }

    SHIPYARD_BUFFER_INLINE unsigned size() const { return mSize; }
    SHIPYARD_BUFFER_INLINE bool empty() const { return mSize == 0; }
    SHIPYARD_BUFFER_INLINE void clear() { mSize = 0; }
    SHIPYARD_BUFFER_INLINE Record& operator[](unsigned index) { return mRecords[index]; }
    SHIPYARD_BUFFER_INLINE const Record& operator[](unsigned index) const { return mRecords[index]; }

    SHIPYARD_BUFFER_COLD bool reserve(unsigned requested) {
        if (requested > Limit) return false;
        if (requested <= mCapacity) return true;
        Record* replacement = static_cast<Record*>(malloc(size_t(requested) * sizeof(Record)));
        if (!replacement) return false;
        // Start each preserved POD object's lifetime before copying its bytes.
        // Trivial default initialization emits no record stores; optimizing
        // compilers can discard this loop while retaining the POD memcpy.
        for (unsigned i = 0; i < mSize; ++i) new (replacement + i) Record;
        if (mSize) memcpy(replacement, mRecords, size_t(mSize) * sizeof(Record));
        free(mRecords);
        mRecords = replacement;
        mCapacity = requested;
        return true;
    }

    SHIPYARD_BUFFER_INLINE Record* AppendUninitialized() {
        if (mSize == mCapacity && !Grow()) return 0;
        // Trivial default initialization starts the POD lifetime and emits no
        // record stores. The caller initializes it before PublishAppend().
        return new (mRecords + mSize) Record;
    }

    // Precondition: successful append and every field assigned by the owner.
    SHIPYARD_BUFFER_INLINE void PublishAppend() { ++mSize; }

private:
    static_assert(sizeof(Record) != 0 && __is_pod(Record), "InstructionBuffer requires POD records");
    static_assert(Limit > 0 && Limit <= size_t(-1) / sizeof(Record), "bounded instruction allocation");
    InstructionBuffer(const InstructionBuffer&);
    InstructionBuffer& operator=(const InstructionBuffer&);

    SHIPYARD_BUFFER_COLD bool Grow() {
        if (mCapacity == Limit) return false;
        unsigned requested = mCapacity ? mCapacity * 2 : 16384;
        if (requested > Limit || requested < mCapacity) requested = Limit;
        return reserve(requested);
    }

    Record* mRecords;
    unsigned mSize, mCapacity;
};

} }

#undef SHIPYARD_BUFFER_INLINE
#undef SHIPYARD_BUFFER_COLD
