#pragma once

#include <stdint.h>
#include <stddef.h>
#include "ring_limits.h"

namespace Fast { namespace ShipyardXenos {

// QueryBufferSpace counts recorded commands. DrawPrimitiveUP vertex data is
// allocated separately by the SDK, so retain its bytes until EndTiling too.
struct TilingBudget {
    enum {
        Limit = TilingPassLimit,
        ExpectedCapacity = RingSecondaryBytes,
        CapacityTolerance = 64 * 1024,
        MaximumInput = 256 * 1024,
        // D3DRING_BUFFER_PARAMETERS permits segments up to 512 KiB. Include
        // one segment reserved ahead of the command cursor and its alignment.
        SegmentReserve = 512 * 1024 + 31,
        StateAllowance = 8192
    };
    uint64_t capacity, vertexBytes;
    unsigned lastUsed;
    bool active, pending;

    TilingBudget() : capacity(0), vertexBytes(0), lastUsed(0), active(false), pending(false) {}
    void Close() { active = pending = false; }
    void Begin(unsigned used, unsigned remaining) {
        Close();
        capacity = uint64_t(used) + remaining;
        vertexBytes = 0;
        lastUsed = used;
        active = capacity >= ExpectedCapacity - CapacityTolerance &&
                 capacity <= ExpectedCapacity + CapacityTolerance;
    }
    void ChargeVertices(uint64_t bytes) {
        if (!active) return;
        // BeginVertices requests 16-byte aligned allocations, including the
        // full-screen restore quad. Saturation disables reconciliation.
        if (bytes > Limit || vertexBytes > Limit - bytes ||
            vertexBytes + bytes > Limit - 15) {
            Close();
            return;
        }
        vertexBytes += bytes + 15;
    }
    bool Valid(unsigned used, unsigned remaining, uintptr_t cursor, uintptr_t end) const {
        return active && uint64_t(used) + remaining == capacity && used >= lastUsed &&
               cursor && end >= cursor && end - cursor <= SegmentReserve - 31 &&
               ((cursor | end) & 3) == 0 &&
               remaining >= ExpectedCapacity / 2 + CapacityTolerance;
    }
    bool BeforeDraw(unsigned used, unsigned remaining, uintptr_t cursor, uintptr_t end,
                    uint64_t incoming, size_t* estimate) {
        if (!estimate || pending || incoming > MaximumInput ||
            !Valid(used, remaining, cursor, end)) return false;
        const uint64_t accounted = uint64_t(used) + vertexBytes + SegmentReserve;
        if (accounted + incoming + 15 + StateAllowance > Limit) return false;
        // The caller retains its original incoming payload + state charge.
        *estimate = (size_t)accounted + 15;
        lastUsed = used;
        pending = true;
        return true;
    }
    bool AfterDraw(unsigned used, unsigned remaining, uintptr_t cursor, uintptr_t end) {
        if (!pending || !Valid(used, remaining, cursor, end) ||
            uint64_t(used) + vertexBytes + SegmentReserve + 15 + StateAllowance > Limit) {
            Close();
            return false;
        }
        pending = false;
        lastUsed = used;
        return true;
    }
};

}} // namespace Fast::ShipyardXenos
