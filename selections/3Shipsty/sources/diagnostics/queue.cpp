#include "queue.h"
#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>

namespace Shipyard {
namespace Diagnostics {
namespace {
struct SectionLock {
    explicit SectionLock(CRITICAL_SECTION& section) : section_(section) {
        EnterCriticalSection(&section_);
    }
    ~SectionLock() {
        LeaveCriticalSection(&section_);
    }

private:
    SectionLock(const SectionLock&);
    SectionLock& operator=(const SectionLock&);
    CRITICAL_SECTION& section_;
};
void ReportFailure(const Config& config) {
    try {
        if (config.sink_error)
            config.sink_error(config.context);
    } catch (...) {
        // A secondary reporting failure must not escape the queue.
    }
}
} // namespace

Queue::Queue(const Config& config)
    : config_(config), wake_(NULL), drained_(NULL), space_(NULL), worker_(NULL), worker_id_(0),
      started_(false), stopping_(false), closed_(false), queued_bytes_(0) {
    if (!config_.consume || !config_.flush || !config_.byte_limit || !config_.line_limit ||
        !config_.interval_ms)
        throw std::invalid_argument("invalid diagnostic queue configuration");
    stats_.accepted = 0;
    stats_.processed = 0;
    stats_.backpressure = 0;
    stats_.peak_bytes = 0;
    InitializeCriticalSection(&gate_);
    InitializeCriticalSection(&sink_gate_);
    wake_ = CreateEvent(NULL, FALSE, FALSE, NULL);
    drained_ = CreateEvent(NULL, TRUE, TRUE, NULL);
    space_ = CreateEvent(NULL, TRUE, TRUE, NULL);
}

Queue::~Queue() {
    shutdown();
    if (worker_)
        CloseHandle(worker_);
    if (wake_)
        CloseHandle(wake_);
    if (drained_)
        CloseHandle(drained_);
    if (space_)
        CloseHandle(space_);
    DeleteCriticalSection(&sink_gate_);
    DeleteCriticalSection(&gate_);
}

void Queue::start_locked() {
    if (started_ || closed_)
        return;
    started_ = true;
    if (config_.force_synchronous || !wake_ || !drained_ || !space_)
        return;
    worker_ = CreateThread(NULL, 64 * 1024, run, this, 0, &worker_id_);
}

void Queue::consume_record(const Record& record) {
    SectionLock lock(sink_gate_);
    try {
        config_.consume(record, config_.context);
    } catch (...) {
        ReportFailure(config_);
    }
}
void Queue::flush_sink() {
    SectionLock lock(sink_gate_);
    try {
        config_.flush(config_.context);
    } catch (...) {
        ReportFailure(config_);
    }
}

bool Queue::submit(const Record& source, uint32_t capture_start) {
    const size_t recordBytes = source.bytes.size() + 1;
    if (recordBytes > config_.byte_limit)
        return false;
    Record ownedRecord;
    try {
        ownedRecord = source;
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
    for (;;) {
        {
            SectionLock lock(gate_);
            start_locked();
            const bool synchronous = !worker_ || closed_;
            const bool room = !synchronous && recordBytes <= config_.byte_limit - queued_bytes_ &&
                              stats_.accepted - stats_.processed < config_.line_limit;
            const bool worker_fallback =
                !synchronous && !room && GetCurrentThreadId() == worker_id_;
            if (synchronous || room || worker_fallback) {
                try {
                    if (config_.begin_record)
                        config_.begin_record(ownedRecord, ownedRecord.metadata, config_.context);
                    if (config_.accepted)
                        config_.accepted(ownedRecord, capture_start, ownedRecord.metadata,
                                         config_.context);
                } catch (...) {
                    return false;
                }
                if (room) {
                    const bool wake_for_record = ownedRecord.format || ownedRecord.level >= 3;
                    try {
                        pending_.push_back(std::move(ownedRecord));
                    } catch (const std::bad_alloc&) {
                        return false;
                    } catch (const std::length_error&) {
                        return false;
                    }
                    queued_bytes_ += recordBytes;
                    ++stats_.accepted;
                    if (queued_bytes_ > stats_.peak_bytes)
                        stats_.peak_bytes = queued_bytes_;
                    ResetEvent(drained_);
                    if (wake_for_record || queued_bytes_ >= config_.wake_bytes)
                        SetEvent(wake_);
                    return true;
                }
                ++stats_.accepted;
                // The fallback serializes sink writes under the queue lock.
                // CRITICAL_SECTION recursion permits one-level report logs.
                consume_record(ownedRecord);
                flush_sink();
                ++stats_.processed;
                return true;
            }
            ++stats_.backpressure;
            ResetEvent(space_);
            SetEvent(wake_);
        }
        WaitForSingleObject(space_, INFINITE);
    }
}

DWORD WINAPI Queue::run(void* context) {
    static_cast<Queue*>(context)->run_worker();
    return 0;
}
void Queue::run_worker() {
    try {
        if (config_.worker_started)
            config_.worker_started(config_.context);
    } catch (...) {
        ReportFailure(config_);
    }
    for (;;) {
        WaitForSingleObject(wake_, config_.interval_ms);
        std::deque<Record> batch;
        EnterCriticalSection(&gate_);
        if (pending_.empty() && stopping_) {
            closed_ = true;
            SetEvent(drained_);
            SetEvent(space_);
            LeaveCriticalSection(&gate_);
            break;
        }
        batch.swap(pending_);
        LeaveCriticalSection(&gate_);
        size_t batchBytes = 0;
        for (size_t i = 0; i < batch.size(); ++i) {
            consume_record(batch[i]);
            batchBytes += batch[i].bytes.size() + 1;
        }
        if (!batch.empty())
            flush_sink();
        EnterCriticalSection(&gate_);
        queued_bytes_ -= batchBytes;
        stats_.processed += static_cast<unsigned>(batch.size());
        if (stats_.processed == stats_.accepted)
            SetEvent(drained_);
        SetEvent(space_);
        if (stopping_)
            SetEvent(wake_);
        LeaveCriticalSection(&gate_);
    }
    try {
        if (config_.worker_stopped)
            config_.worker_stopped(config_.context);
    } catch (...) {
        ReportFailure(config_);
    }
}

void Queue::flush() {
    HANDLE drainEvent = NULL;
    EnterCriticalSection(&gate_);
    if (worker_ && !closed_ && GetCurrentThreadId() != worker_id_) {
        drainEvent = drained_;
        SetEvent(wake_);
    }
    LeaveCriticalSection(&gate_);
    if (drainEvent)
        WaitForSingleObject(drainEvent, INFINITE);
}
void Queue::shutdown() {
    HANDLE workerToJoin = NULL;
    EnterCriticalSection(&gate_);
    if (worker_) {
        if (!closed_) {
            stopping_ = true;
            SetEvent(wake_);
        }
        // closed_ only means that the queue drained. The final worker
        // callback may still be running, so every external caller joins.
        if (GetCurrentThreadId() != worker_id_)
            workerToJoin = worker_;
    } else
        closed_ = true;
    LeaveCriticalSection(&gate_);
    if (workerToJoin)
        WaitForSingleObject(workerToJoin, INFINITE);
}
Stats Queue::stats() {
    EnterCriticalSection(&gate_);
    Stats result = stats_;
    LeaveCriticalSection(&gate_);
    return result;
}

} // namespace Diagnostics
} // namespace Shipyard
