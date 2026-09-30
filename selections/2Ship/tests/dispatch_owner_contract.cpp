#include "fast3d/dispatch_owner.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <thread>

struct Object { explicit Object(int n) : id(n) {} int id; };
typedef Shipyard::Fast3D::DispatchOwner<Object> Scope;
static thread_local Scope* active = 0;
static Object* original(const std::weak_ptr<Object>& selected) { return selected.lock().get(); }
static Object* projected(const std::weak_ptr<Object>& selected) {
    return active ? active->Get(selected) : Scope::TemporaryBorrow(selected);
}
static void same(const std::weak_ptr<Object>& selected) { assert(projected(selected) == original(selected)); }

int main() {
    std::shared_ptr<Object> a(new Object(1)), b(new Object(2));
    std::weak_ptr<Object> selected = a;
    same(selected);
    {
        Scope scope(active, selected);
        assert(active == &scope && a.use_count() == 2);
        for (int i = 0; i < 10000; ++i) same(selected);
        try {
            Scope inner(active, selected);
            assert(a.use_count() == 3);
            same(selected);
            throw std::runtime_error("test");
        } catch (const std::runtime_error&) {}
        assert(active == &scope && a.use_count() == 2);
        std::thread other([&] { assert(active == 0); same(selected); });
        other.join();
        {
            Scope inner(active, selected);
            selected = b;
            Scope::Invalidate(active);
            assert(a.use_count() == 1);
            same(selected);
            assert(b.use_count() == 1);
        }
        assert(active == &scope);
        same(selected);
    }
    assert(active == 0 && a.use_count() == 1 && b.use_count() == 1);
    {
        Scope scope(active, selected);
        b.reset();
        assert(original(selected)->id == 2);
        assert(projected(selected)->id == 2);
    }
    assert(selected.expired());
    same(selected);
    { Scope empty(active, selected); same(selected); }
    assert(active == 0);
    puts("10000 original-vs-projected borrows; TLS, nested exceptions, replacement, expiration and dispatch pin passed");
}
