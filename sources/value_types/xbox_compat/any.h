#pragma once
#include "common.h"
#include <typeinfo>

namespace xbox_compat {
// Copyable, type-erased ownership for callers that use pointer any_cast.
// The payload's own copy constructor and destructor retain its value semantics.
class any {
    struct payload {
        virtual ~payload() {}
        virtual payload* clone() const = 0;
        virtual const std::type_info& type() const = 0;
        virtual void* address() = 0;
    };
    template<class T> struct value_payload : payload {
        T value;
        explicit value_payload(const T& input) : value(input) {}
        payload* clone() const { return new value_payload(value); }
        const std::type_info& type() const { return typeid(T); }
        void* address() { return std::addressof(value); }
    };
    payload* value_;
public:
    any() : value_(0) {}
    any(const any& other) : value_(other.value_ ? other.value_->clone() : 0) {}
    any(any&& other) : value_(other.value_) { other.value_ = 0; }
    template<class T> any(const T& value)
        : value_(new value_payload<typename value_detail::decay<const T>::type>(value)) {}
    ~any() { delete value_; }
    void swap(any& other) { payload* previous = value_; value_ = other.value_; other.value_ = previous; }
    any& operator=(const any& other) { any copy(other); swap(copy); return *this; }
    any& operator=(any&& other) { any moved(value_detail::move(other)); swap(moved); return *this; }
    template<class T> any& operator=(const T& value) { any copy(value); swap(copy); return *this; }
    void reset() { delete value_; value_ = 0; }
    bool has_value() const { return value_ != 0; }
    const std::type_info& type() const { return value_ ? value_->type() : typeid(void); }
    template<class T> T* pointer() {
        return value_ && value_->type() == typeid(T) ? static_cast<T*>(value_->address()) : 0;
    }
    template<class T> const T* pointer() const {
        return value_ && value_->type() == typeid(T) ? static_cast<const T*>(value_->address()) : 0;
    }
};
template<class T> T* any_cast(any* value) { return value ? value->pointer<T>() : 0; }
template<class T> const T* any_cast(const any* value) { return value ? value->pointer<T>() : 0; }
} // namespace xbox_compat
