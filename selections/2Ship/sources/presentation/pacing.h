#pragma once
#include <stdint.h>
#include <stdexcept>

namespace Shipyard {
class PresentationPacing {
  public:
    PresentationPacing() : rate(60), refresh(60), remainder(0) {}
    void SetRate(uint32_t value) {
        if (!value) throw std::invalid_argument("presentation rate must be positive");
        if (rate != value) { rate = value; remainder = 0; }
    }
    void SetRefresh(uint32_t value) {
        if (!value) throw std::invalid_argument("display refresh must be positive");
        if (refresh != value) { refresh = value; remainder = 0; }
    }
    uint32_t Refresh() const { return refresh; }
    uint32_t Next() {
        const uint64_t total = uint64_t(remainder) + refresh;
        remainder = static_cast<uint32_t>(total % rate);
        return static_cast<uint32_t>(total / rate);
    }
  private:
    uint32_t rate, refresh, remainder;
};
}
