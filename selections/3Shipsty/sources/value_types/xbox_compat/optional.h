#ifndef XBOX_COMPAT_OPTIONAL_H
#define XBOX_COMPAT_OPTIONAL_H
#include "common.h"
namespace xbox_compat {
struct nullopt_t { explicit nullopt_t(int) {} };
static const nullopt_t nullopt(0);
template<class T> class optional {
    typedef typename std::aligned_storage<sizeof(T),std::alignment_of<T>::value>::type storage_type;
    typedef char storage_alignment_check[(std::alignment_of<storage_type>::value >= std::alignment_of<T>::value)?1:-1];
    storage_type storage_;
    bool engaged_;
    T* ptr() { return reinterpret_cast<T*>(&storage_); }
    const T* ptr() const { return reinterpret_cast<const T*>(&storage_); }
    template<class U> void construct(U&& value) {
        ::new(static_cast<void*>(&storage_)) T(value_detail::forward<U>(value));
        engaged_=true;
    }
    void bool_anchor() const {}
public:
    optional():engaged_(false) {}
    optional(nullopt_t):engaged_(false) {}
    optional(const optional& other):engaged_(false) { if(other.engaged_) construct(*other.ptr()); }
    optional(optional&& other):engaged_(false) { if(other.engaged_) construct(value_detail::move(*other.ptr())); }
    template<class U> optional(U&& value,typename std::enable_if<!std::is_same<typename value_detail::decay<U>::type,optional>::value && std::is_convertible<U,T>::value,int>::type=0):engaged_(false) {
        construct(value_detail::forward<U>(value));
    }
    ~optional() { reset(); }
    optional& operator=(nullopt_t) { reset();return *this; }
    optional& operator=(const optional& other) {
        if(this==&other) return *this;
        if(!other.engaged_) reset();
        else if(engaged_) *ptr()=*other.ptr();
        else construct(*other.ptr());
        return *this;
    }
    optional& operator=(optional&& other) {
        if(this==&other) return *this;
        if(!other.engaged_) reset();
        else if(engaged_) *ptr()=value_detail::move(*other.ptr());
        else construct(value_detail::move(*other.ptr()));
        return *this;
    }
    template<class U> typename std::enable_if<!std::is_same<typename value_detail::decay<U>::type,optional>::value && std::is_convertible<U,T>::value,optional&>::type operator=(U&& value) {
        if(engaged_) *ptr()=value_detail::forward<U>(value);
        else construct(value_detail::forward<U>(value));
        return *this;
    }
    bool has_value() const { return engaged_; }
    typedef void (optional::*safe_bool)() const;
    operator safe_bool() const { return engaged_?&optional::bool_anchor:0; }
    T& value() { if(!engaged_)throw bad_optional_access();return *ptr(); }
    const T& value() const { if(!engaged_)throw bad_optional_access();return *ptr(); }
    T& operator*() { return *ptr(); }
    const T& operator*() const { return *ptr(); }
    T* operator->() { return ptr(); }
    const T* operator->() const { return ptr(); }
    template<class U> T value_or(U&& fallback) const { return engaged_?*ptr():static_cast<T>(value_detail::forward<U>(fallback)); }
    void reset() { if(engaged_){ engaged_=false;ptr()->~T(); } }
};
}
#endif
