#pragma once
#include <functional>
namespace Shipyard { namespace Async {
// Own the callable until the native worker finishes. Creation errors propagate;
// uncaught worker exceptions terminate, matching std::thread's contract.
void launch_detached(const std::function<void()>& callable);
// Application-thread shutdown barrier. Stop external producers first; captures
// are destroyed before this returns. Never call from a tracked worker/capture.
// This drains work without changing detached native-handle ownership.
void wait_for_detached();
} }
