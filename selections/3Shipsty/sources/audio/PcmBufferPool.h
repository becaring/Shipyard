#ifndef SHIPYARD_AUDIO_PCM_BUFFER_POOL_H
#define SHIPYARD_AUDIO_PCM_BUFFER_POOL_H

#include "async/native.h"

namespace ShipyardAudio {

// One submitting owner; XAudio callbacks may release blocks concurrently.
class PcmBufferPool {
public:
    enum { BlockCount = 8 };
    struct Block {
        volatile LONG busy;
        BYTE* samples;
        PcmBufferPool* owner;
    };
    PcmBufferPool();
    ~PcmBufferPool();
    bool Initialize(unsigned blockBytes);
    void Close(); // Voice must be destroyed first; no callback may still run.
    Block* TryAcquire();
    void Release(Block* block);
    bool WaitAvailable(DWORD milliseconds);
    void Wake();
    unsigned CapacityBytes() const { return capacity_; }
private:
    PcmBufferPool(const PcmBufferPool&);
    PcmBufferPool& operator=(const PcmBufferPool&);
    Block blocks_[BlockCount];
    HANDLE available_;
    unsigned capacity_;
};

}

#endif
