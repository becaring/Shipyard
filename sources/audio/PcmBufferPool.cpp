#include "PcmBufferPool.h"
#include <malloc.h>

namespace ShipyardAudio {
PcmBufferPool::PcmBufferPool() : available_(NULL), capacity_(0) {
    for (unsigned i = 0; i < BlockCount; ++i) {
        blocks_[i].busy = 0; blocks_[i].samples = NULL; blocks_[i].owner = this;
    }
}
PcmBufferPool::~PcmBufferPool() { Close(); }
bool PcmBufferPool::Initialize(unsigned blockBytes) {
    Close();
    if (!blockBytes) return false;
    available_ = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!available_) return false;
    for (unsigned i = 0; i < BlockCount; ++i) {
        blocks_[i].samples = static_cast<BYTE*>(_aligned_malloc(blockBytes, 16));
        if (!blocks_[i].samples) { Close(); return false; }
    }
    capacity_ = blockBytes;
    return true;
}
void PcmBufferPool::Close() {
    for (unsigned i = 0; i < BlockCount; ++i) {
        _aligned_free(blocks_[i].samples);
        blocks_[i].samples = NULL; blocks_[i].busy = 0;
    }
    if (available_) CloseHandle(available_);
    available_ = NULL; capacity_ = 0;
}
PcmBufferPool::Block* PcmBufferPool::TryAcquire() {
    if (!available_) return NULL;
    for (unsigned i = 0; i < BlockCount; ++i) {
        if (InterlockedCompareExchange(&blocks_[i].busy, 1, 0) == 0) return &blocks_[i];
    }
    return NULL;
}
void PcmBufferPool::Release(Block* block) {
    InterlockedExchange(&block->busy, 0);
    Wake();
}
bool PcmBufferPool::WaitAvailable(DWORD milliseconds) {
    DWORD result = WaitForSingleObject(available_, milliseconds);
    if (result == WAIT_OBJECT_0) return true;
    if (result == WAIT_TIMEOUT) return false;
    throw Shipyard::Async::native_error("audio buffer wait", GetLastError());
}
void PcmBufferPool::Wake() {
    if (!SetEvent(available_)) throw Shipyard::Async::native_error("audio buffer notification", GetLastError());
}
}
