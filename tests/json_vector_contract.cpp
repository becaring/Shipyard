#include <nlohmann/json.hpp>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <vector>

static size_t allocation_count;
static size_t live_allocations;
static bool fail_allocation;

template <class T> struct CountingAllocator {
    typedef T value_type;
    typedef T* pointer;
    typedef const T* const_pointer;
    typedef T& reference;
    typedef const T& const_reference;
    typedef size_t size_type;
    typedef ptrdiff_t difference_type;
    template <class U> struct rebind { typedef CountingAllocator<U> other; };
    CountingAllocator() {}
    template <class U> CountingAllocator(const CountingAllocator<U>&) {}
    T* allocate(size_t count, const void* = 0) {
        if (fail_allocation)
            throw std::bad_alloc();
        T* result = std::allocator<T>().allocate(count);
        ++allocation_count;
        ++live_allocations;
        return result;
    }
    void deallocate(T* data, size_t count) {
        --live_allocations;
        std::allocator<T>().deallocate(data, count);
    }
    void construct(T* data, const T& value) { new (data) T(value); }
    void destroy(T* data) { data->~T(); }
    size_t max_size() const { return size_t(-1) / sizeof(T); }
};
template <class T, class U>
bool operator==(const CountingAllocator<T>&, const CountingAllocator<U>&) { return true; }
template <class T, class U>
bool operator!=(const CountingAllocator<T>&, const CountingAllocator<U>&) { return false; }

static void require(bool value, const char* message) {
    if (!value) {
        std::fprintf(stderr, "FAIL %s\n", message);
        std::exit(1);
    }
}

template <class Json> void check(const char* kind) {
    typedef std::vector<int, CountingAllocator<int> > Vector;
    const size_t sizes[] = {0, 1, 64, 1024};
    for (size_t test = 0; test < sizeof(sizes) / sizeof(sizes[0]); ++test) {
        const size_t count = sizes[test];
        Json input = Json::array();
        for (size_t i = 0; i < count; ++i)
            input.push_back(int(i) - 512);
        const std::string before = input.dump();
        allocation_count = 0;
        {
            Vector result = input.template get<Vector>();
            require(result.size() == count, "vector size");
            for (size_t i = 0; i < count; ++i)
                require(result[i] == int(i) - 512, "element value or order");
            std::printf("%s count=%u allocations=%u\n", kind, unsigned(count),
                        unsigned(allocation_count));
            require(allocation_count == (count ? 1u : 0u), "single vector allocation");
            require(input.dump() == before, "source unchanged by conversion");
            input = Json();
            if (count)
                require(result.back() == int(count - 1) - 512, "result ownership");
        }
        require(live_allocations == 0, "vector storage released");
    }

    Json wrong = Json::object();
    allocation_count = 0;
    try { (void)wrong.template get<Vector>(); require(false, "object conversion accepted"); }
    catch (const typename Json::type_error& error) { require(error.id == 302, "object error id"); }
    require(allocation_count == 0, "allocation before type validation");

    Json mixed = Json::array();
    mixed.push_back(7);
    mixed.push_back("invalid integer");
    const std::string before = mixed.dump();
    try { (void)mixed.template get<Vector>(); require(false, "invalid element accepted"); }
    catch (const typename Json::type_error& error) { require(error.id == 302, "element error id"); }
    require(live_allocations == 0 && mixed.dump() == before, "conversion failure ownership");

    fail_allocation = true;
    try { (void)mixed.template get<Vector>(); require(false, "allocation failure ignored"); }
    catch (const std::bad_alloc&) {}
    fail_allocation = false;
    require(live_allocations == 0 && mixed.dump() == before, "allocation failure ownership");
}

int main() {
    check<nlohmann::json>("ordinary");
    check<nlohmann::ordered_json>("ordered");
    std::puts("JSON vector values, ownership, exceptions, and allocation counts PASS");
}
