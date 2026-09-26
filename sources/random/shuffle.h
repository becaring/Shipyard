#pragma once
#include <algorithm>
#include <iterator>
#include <random>

namespace Shipyard {
namespace Random {

// The legacy XDK has uniform_int but predates std::shuffle. Keep the caller's
// engine and use the standard library's unbiased bounded distribution.
template <class Iterator, class Engine> void Shuffle(Iterator first, Iterator last, Engine& engine) {
    typedef typename std::iterator_traits<Iterator>::difference_type Difference;
    Difference remaining = last - first;
    while (remaining > 1) {
#if defined(_XBOX)
        std::uniform_int<Difference> choose(0, remaining - 1);
#else
        std::uniform_int_distribution<Difference> choose(0, remaining - 1);
#endif
        const Difference selected = choose(engine);
        --remaining;
        std::iter_swap(first + remaining, first + selected);
    }
}

} // namespace Random
} // namespace Shipyard
