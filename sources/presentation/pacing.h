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

// GUI capture can run once for several presents; measure display cadence separately.
class PresentationCadence {
  public:
    PresentationCadence() : start(0), count(0), fps(0), started(false) {}
    void Observe(uint64_t now, uint64_t clockFrequency) {
        if (!clockFrequency) return;
        if (!started || now < start) {
            start = now;
            count = 0;
            fps = 0;
            started = true;
            return;
        }
        ++count;
        const uint64_t elapsed = now - start;
        if (elapsed && elapsed >= clockFrequency / 2) {
            fps = static_cast<float>(double(count) * double(clockFrequency) / double(elapsed));
            start = now;
            count = 0;
        }
    }
    float Fps() const { return fps; }
  private:
    uint64_t start;
    unsigned count;
    float fps;
    bool started;
};
}
