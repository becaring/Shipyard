#include <nlohmann/json.hpp>
#include <new>
#include <cstdlib>
#include <cstdio>
#include <string>

static size_t allocations = 0;
void* operator new(size_t size) {
    ++allocations;
    void* result = std::malloc(size ? size : 1);
    if (!result) throw std::bad_alloc();
    return result;
}
void operator delete(void* value) noexcept { std::free(value); }
void* operator new[](size_t size) {
    ++allocations;
    void* result = std::malloc(size ? size : 1);
    if (!result) throw std::bad_alloc();
    return result;
}
void operator delete[](void* value) noexcept { std::free(value); }

template<class Json> int check() {
    Json object = Json::object();
    object["short"] = "ok";
    object["number"] = 42;
    std::string large(1024, 'z');
    size_t before = allocations;
    if (object.value("short", large.c_str()) != "ok") return 1;
    if (allocations != before) return 2;
    if (object.value("number", -1) != 42) return 3;
    if (object.value("missing", large.c_str()) != large) return 4;
    if (object.value("absent", 19) != 19) return 5;
    try { (void)object.value("number", "fallback"); return 6; }
    catch (const typename Json::type_error& error) { if (error.id != 302) return 7; }
    Json scalar = 3;
    try { (void)scalar.value("short", 0); return 8; }
    catch (const typename Json::type_error& error) { if (error.id != 306) return 9; }
    return 0;
}

int main() {
    int ordinary = check<nlohmann::json>();
    int ordered = check<nlohmann::ordered_json>();
    if (ordinary || ordered) return ordinary ? 10 + ordinary : 20 + ordered;
    std::puts("ordinary and ordered JSON value semantics/allocation PASS");
    return 0;
}
