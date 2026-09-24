#include "queue.h"
#include <algorithm>
#include <new>
#include <stdexcept>

namespace Shipyard { namespace Diagnostics {

Queue::Queue(const Config& config) : config_(config), wake_(NULL), drained_(NULL),
    space_(NULL), worker_(NULL), worker_id_(0), started_(false), stopping_(false),
    closed_(false), queued_bytes_(0) {
    if (!config_.consume || !config_.flush || !config_.byte_limit ||
        !config_.line_limit || !config_.interval_ms)
        throw std::invalid_argument("invalid diagnostic queue configuration");
    stats_.accepted = stats_.processed = stats_.backpressure = 0;
    stats_.peak_bytes = 0;
    InitializeCriticalSection(&gate_);
    InitializeCriticalSection(&sink_gate_);
    wake_ = CreateEvent(NULL, FALSE, FALSE, NULL);
    drained_ = CreateEvent(NULL, TRUE, TRUE, NULL);
    space_ = CreateEvent(NULL, TRUE, TRUE, NULL);
}

Queue::~Queue() {
    shutdown();
    if (worker_) CloseHandle(worker_);
    if (wake_) CloseHandle(wake_);
    if (drained_) CloseHandle(drained_);
    if (space_) CloseHandle(space_);
    DeleteCriticalSection(&sink_gate_);
    DeleteCriticalSection(&gate_);
}

void Queue::start_locked() {
    if (started_ || closed_) return;
    started_ = true;
    if (config_.force_synchronous || !wake_ || !drained_ || !space_) return;
    worker_ = CreateThread(NULL, 64 * 1024, run, this, 0, &worker_id_);
}

void Queue::consume_record(const Record& record) {
    EnterCriticalSection(&sink_gate_);
    try { config_.consume(record, config_.context); }
    catch (...) { if (config_.sink_error) config_.sink_error(config_.context); }
    LeaveCriticalSection(&sink_gate_);
}
void Queue::flush_sink() {
    EnterCriticalSection(&sink_gate_);
    try { config_.flush(config_.context); }
    catch (...) { if (config_.sink_error) config_.sink_error(config_.context); }
    LeaveCriticalSection(&sink_gate_);
}

bool Queue::submit(const Record& source, uint32_t capture_start) {
    const size_t bytes = source.bytes.size() + 1;
    if (bytes > config_.byte_limit) return false;
    Record owned;
    try { owned = source; }
    catch (const std::bad_alloc&) { return false; }
    catch (const std::length_error&) { return false; }
    for (;;) {
        EnterCriticalSection(&gate_);
        start_locked();
        if (!worker_ || closed_) {
            ++stats_.accepted;
            if (config_.begin_record) config_.begin_record(owned, config_.context);
            if (config_.accepted) config_.accepted(owned, capture_start, config_.context);
            // The original fallback serializes sink writes under its queue
            // lock. CRITICAL_SECTION recursion permits one-level report logs.
            consume_record(owned);
            flush_sink();
            ++stats_.processed;
            LeaveCriticalSection(&gate_);
            return true;
        }
        if (bytes <= config_.byte_limit - queued_bytes_ &&
            stats_.accepted - stats_.processed < config_.line_limit) {
            if (config_.begin_record) config_.begin_record(owned, config_.context);
            try { pending_.push_back(owned); }
            catch (const std::bad_alloc&) { LeaveCriticalSection(&gate_); return false; }
            catch (const std::length_error&) { LeaveCriticalSection(&gate_); return false; }
            queued_bytes_ += bytes;
            ++stats_.accepted;
            if (config_.accepted) config_.accepted(pending_.back(), capture_start, config_.context);
            if (queued_bytes_ > stats_.peak_bytes) stats_.peak_bytes = queued_bytes_;
            ResetEvent(drained_);
            if (owned.format || owned.level >= 3 || queued_bytes_ >= config_.wake_bytes)
                SetEvent(wake_);
            LeaveCriticalSection(&gate_);
            return true;
        }
        // The worker may log from a sink or its startup callback. Preserve
        // normal FIFO queueing when room remains, but never wait on itself.
        if (GetCurrentThreadId() == worker_id_) {
            ++stats_.accepted;
            if (config_.begin_record) config_.begin_record(owned, config_.context);
            if (config_.accepted) config_.accepted(owned, capture_start, config_.context);
            consume_record(owned);
            flush_sink();
            ++stats_.processed;
            LeaveCriticalSection(&gate_);
            return true;
        }
        ++stats_.backpressure;
        ResetEvent(space_);
        SetEvent(wake_);
        LeaveCriticalSection(&gate_);
        WaitForSingleObject(space_, INFINITE);
    }
}

DWORD WINAPI Queue::run(void* context) {
    static_cast<Queue*>(context)->run_worker(); return 0;
}
void Queue::run_worker() {
    if (config_.worker_started) config_.worker_started(config_.context);
    for (;;) {
        WaitForSingleObject(wake_, config_.interval_ms);
        std::deque<Record> lines;
        EnterCriticalSection(&gate_);
        if (pending_.empty() && stopping_) {
            closed_ = true;
            SetEvent(drained_); SetEvent(space_);
            LeaveCriticalSection(&gate_);
            break;
        }
        lines.swap(pending_);
        LeaveCriticalSection(&gate_);
        size_t bytes = 0;
        for (size_t i = 0; i < lines.size(); ++i) {
            consume_record(lines[i]);
            bytes += lines[i].bytes.size() + 1;
        }
        if (!lines.empty()) flush_sink();
        EnterCriticalSection(&gate_);
        queued_bytes_ -= bytes;
        stats_.processed += static_cast<unsigned>(lines.size());
        if (stats_.processed == stats_.accepted) SetEvent(drained_);
        SetEvent(space_);
        if (stopping_) SetEvent(wake_);
        LeaveCriticalSection(&gate_);
    }
    if (config_.worker_stopped) config_.worker_stopped(config_.context);
}

void Queue::flush() {
    HANDLE wait = NULL;
    EnterCriticalSection(&gate_);
    if (worker_ && !closed_ && GetCurrentThreadId() != worker_id_) {
        wait = drained_; SetEvent(wake_);
    }
    LeaveCriticalSection(&gate_);
    if (wait) WaitForSingleObject(wait, INFINITE);
}
void Queue::shutdown() {
    HANDLE wait = NULL;
    EnterCriticalSection(&gate_);
    if (worker_ && !closed_ && GetCurrentThreadId() != worker_id_) {
        stopping_ = true;
        wait = worker_;
        SetEvent(wake_);
    } else if (!worker_) closed_ = true;
    LeaveCriticalSection(&gate_);
    if (wait) WaitForSingleObject(wait, INFINITE);
}
Stats Queue::stats() {
    EnterCriticalSection(&gate_);
    Stats result = stats_;
    LeaveCriticalSection(&gate_);
    return result;
}

} }
