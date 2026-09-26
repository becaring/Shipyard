#include "xbox_compat/any.h"
#include <cassert>
#include <stdexcept>
#include <string>

struct Value {
    static int alive;
    static bool fail;
    int number;
    Value(int n) : number(n) { ++alive; }
    Value(const Value& value) : number(value.number) {
        if (fail) throw std::runtime_error("copy");
        ++alive;
    }
    ~Value() { --alive; }
    Value* operator&() { return 0; }
};
int Value::alive = 0;
bool Value::fail = false;

int main() {
    using xbox_compat::any;
    using xbox_compat::any_cast;
    assert(any_cast<int>(static_cast<any*>(0)) == 0);
    assert(any_cast<int>(static_cast<const any*>(0)) == 0);
    any empty;
    assert(!empty.has_value() && empty.type() == typeid(void));
    assert(any_cast<int>(&empty) == 0);
    {
        Value original(7);
        any first(original);
        assert(Value::alive == 2 && any_cast<Value>(&first)->number == 7);
        any second(first);
        any_cast<Value>(&first)->number = 9;
        const any& immutable = second;
        assert(any_cast<Value>(&immutable)->number == 7);
        assert(any_cast<const Value>(&first)->number == 9);
        assert(any_cast<std::string>(&first) == 0);
        Value::fail = true;
        try { second = first; assert(false); } catch (const std::runtime_error&) {}
        Value::fail = false;
        assert(any_cast<Value>(&second)->number == 7 && Value::alive == 3);
        second = second;
        assert(Value::alive == 3);
        any moved(xbox_compat::value_detail::move(first));
        assert(!first.has_value() && any_cast<Value>(&moved)->number == 9);
        second = xbox_compat::value_detail::move(moved);
        assert(!moved.has_value() && Value::alive == 2);
        second.reset();
        assert(!second.has_value() && Value::alive == 1);
    }
    assert(Value::alive == 0);
    any text("hello");
    assert(text.type() == typeid(const char*));
    assert(std::string(*any_cast<const char*>(&text)) == "hello");
    text = std::string("owned");
    any copy(text);
    *any_cast<std::string>(&text) = "changed";
    assert(*any_cast<std::string>(&copy) == "owned");
}
