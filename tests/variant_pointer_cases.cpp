#include "value_types/xbox_compat/variant.h"
#include <cassert>
#include <cstdio>
#include <type_traits>

struct AddressOverload { AddressOverload* operator&() { return 0; } };

int main() {
    typedef xbox_compat::variant<int, std::string> Value;
    Value value(7);
    assert(*xbox_compat::get_if<int>(&value) == 7);
    assert(xbox_compat::get_if<std::string>(&value) == 0);
    assert(xbox_compat::get_if<int>(static_cast<Value*>(0)) == 0);
    assert(xbox_compat::get_if<int>(static_cast<const Value*>(0)) == 0);
    *xbox_compat::get_if<int>(&value) = 9;
    assert(xbox_compat::get<int>(value) == 9);
    value = std::string("selected");
    const Value& immutable = value;
    static_assert(std::is_same<decltype(xbox_compat::get_if<std::string>(&immutable)), const std::string*>::value, "const preserved");
    assert(*xbox_compat::get_if<std::string>(&immutable) == "selected");
    assert(xbox_compat::get_if<int>(&immutable) == 0);
    xbox_compat::variant<AddressOverload, int> overloaded;
    assert(xbox_compat::get_if<AddressOverload>(&overloaded) != 0);
    puts("PASS variant pointer access");
}
