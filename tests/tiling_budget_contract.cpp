#include <assert.h>
#include "tiling_budget.h"
using Fast::ShipyardXenos::TilingBudget;

int main() {
    const unsigned capacity = 8 * 1024 * 1024;
    const uintptr_t cursor = 0x100000, end = 0x110000;
    for (unsigned incoming = 0; incoming <= TilingBudget::MaximumInput; incoming += 1024) {
        TilingBudget b;
        b.Begin(0, capacity);
        b.ChargeVertices(96); // framebuffer restore
        b.ChargeVertices(128);
        const unsigned used = TilingBudget::Limit - TilingBudget::SegmentReserve -
            unsigned(b.vertexBytes) - incoming - 15 - TilingBudget::StateAllowance;
        size_t estimate = 0;
        assert(b.BeforeDraw(used, capacity-used, cursor, end, incoming, &estimate));
        assert(estimate + incoming + TilingBudget::StateAllowance == TilingBudget::Limit);
        b.Begin(0, capacity); b.ChargeVertices(96); b.ChargeVertices(128);
        assert(!b.BeforeDraw(used+1, capacity-used-1, cursor, end, incoming, &estimate));
    }
    TilingBudget b; size_t estimate = 123;
    b.Begin(0, capacity);
    assert(!b.BeforeDraw(0,0,cursor,end,16,&estimate));
    assert(!b.BeforeDraw(0,capacity,0,end,16,&estimate));
    assert(!b.BeforeDraw(0,capacity,cursor,cursor-4,16,&estimate));
    assert(!b.BeforeDraw(0,capacity,cursor,cursor+512*1024+4,16,&estimate));
    assert(!b.BeforeDraw(0,capacity,cursor+1,end,16,&estimate));
    assert(!b.BeforeDraw(0,capacity,cursor,end,TilingBudget::MaximumInput+1,&estimate));
    assert(estimate==123);
    b.ChargeVertices(~uint64_t(0)); assert(!b.active);
    b.Begin(0,capacity); b.ChargeVertices(TilingBudget::Limit-15); assert(b.active);
    b.ChargeVertices(1); assert(!b.active);
    b.Begin(0,capacity); b.ChargeVertices(96);
    assert(b.BeforeDraw(100,capacity-100,cursor,end,64,&estimate));
    b.ChargeVertices(64);
    assert(b.AfterDraw(200,capacity-200,cursor,end));
    assert(!b.BeforeDraw(199,capacity-199,cursor,end,16,&estimate));
    assert(b.BeforeDraw(200,capacity-200,cursor,end,16,&estimate));
    assert(!b.AfterDraw(199,capacity-199,cursor,end)); assert(!b.active);
    b.Begin(0,capacity);
    assert(b.BeforeDraw(100,capacity-100,cursor,end,64,&estimate));
    assert(!b.AfterDraw(100,capacity-99,cursor,end)); assert(!b.active);
    b.Begin(0,capacity);
    assert(b.BeforeDraw(100,capacity-100,cursor,end,64,&estimate));
    b.ChargeVertices(TilingBudget::Limit-15);
    assert(!b.AfterDraw(100,capacity-100,cursor,end)); assert(!b.active);
    b.Begin(0,1); assert(!b.active);
    b.Begin(0,capacity); b.ChargeVertices(96); b.Begin(0,capacity);
    assert(b.vertexBytes==0 && !b.pending);
    b.Close(); assert(!b.BeforeDraw(0,capacity,cursor,end,1,&estimate));
}
