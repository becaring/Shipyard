#include "random/shuffle.h"
#include <assert.h>
#include <set>
#include <vector>

struct CountingEngine {
    typedef unsigned result_type;
    unsigned calls;
    CountingEngine() : calls(0) {}
    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return 0xffffffffu; }
    result_type operator()() { ++calls; return 1; }
};

int main() {
    CountingEngine unused;
    int single[] = {9};
    Shipyard::Random::Shuffle(single, single, unused);
    Shipyard::Random::Shuffle(single, single + 1, unused);
    assert(unused.calls == 0 && single[0] == 9);
    std::set<std::vector<int>> permutations;
    for (unsigned seed = 0; seed != 256; ++seed) {
        std::mt19937 first(seed), second(seed);
        int input[] = {0, 1, 2};
        std::vector<int> a(input, input + 3), b(a);
        Shipyard::Random::Shuffle(a.begin(), a.end(), first);
        Shipyard::Random::Shuffle(b.begin(), b.end(), second);
        assert(a == b && first == second);
        permutations.insert(a);
        std::sort(a.begin(), a.end());
        assert(std::equal(a.begin(), a.end(), input));
    }
    assert(permutations.size() == 6);
    std::mt19937 engine(7);
    for (int size = 2; size < 128; ++size) {
        std::vector<int> values(size);
        for (int i = 0; i < size; ++i) values[i] = i;
        Shipyard::Random::Shuffle(values.begin(), values.end(), engine);
        std::sort(values.begin(), values.end());
        for (int i = 0; i < size; ++i) assert(values[i] == i);
    }
    return 0;
}
