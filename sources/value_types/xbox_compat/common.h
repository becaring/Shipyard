#ifndef XBOX_COMPAT_VALUE_COMMON_H
#define XBOX_COMPAT_VALUE_COMMON_H
#include <cstddef>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace xbox_compat {
class bad_variant_access : public std::exception {
public: const char* what() const throw() { return "bad variant access"; }
};
class bad_optional_access : public std::exception {
public: const char* what() const throw() { return "bad optional access"; }
};
static const std::size_t variant_npos = static_cast<std::size_t>(-1);

/* XDK has no noexcept expression or move-construction trait. Explicitly
 * register a guaranteed nonthrowing user move here when lowering such a type.
 * Default XDK classes have no inferred noexcept move contract. A nonthrowing
 * copy alone does not prove that overload resolution chooses that copy. */
#if defined(_MSC_VER) && _MSC_VER < 1900
template<class T> struct is_nothrow_move : std::integral_constant<bool,
    std::is_pod<T>::value> {};
template<class T> struct is_nothrow_copy : std::has_nothrow_copy_constructor<T> {};
template<class T> struct is_nothrow_move<std::shared_ptr<T> > : std::true_type {};
template<class T> struct is_nothrow_copy<std::shared_ptr<T> > : std::true_type {};
template<class T> struct is_nothrow_move<std::weak_ptr<T> > : std::true_type {};
template<class T> struct is_nothrow_copy<std::weak_ptr<T> > : std::true_type {};
template<> struct is_nothrow_move<std::string> : std::integral_constant<bool,(_ITERATOR_DEBUG_LEVEL==0)> {};
template<> struct is_nothrow_move<std::wstring> : std::integral_constant<bool,(_ITERATOR_DEBUG_LEVEL==0)> {};
template<class T> struct is_nothrow_move<std::vector<T,std::allocator<T> > > : std::integral_constant<bool,(_ITERATOR_DEBUG_LEVEL==0)> {};
#else
template<class T> struct is_nothrow_move : std::is_nothrow_move_constructible<T> {};
template<class T> struct is_nothrow_copy : std::is_nothrow_copy_constructible<T> {};
#endif

namespace value_detail {
template<class T> struct remove_reference { typedef T type; };
template<class T> struct remove_reference<T&> { typedef T type; };
template<class T> struct remove_reference<T&&> { typedef T type; };
template<class T> struct decay { typedef typename std::decay<T>::type type; };
template<class T> T&& declval();
template<class T> T&& forward(typename remove_reference<T>::type& value) { return static_cast<T&&>(value); }
template<class T> typename remove_reference<T>::type&& move(T&& value) { return static_cast<typename remove_reference<T>::type&&>(value); }
template<std::size_t A,std::size_t B> struct maximum { enum { value = A>B?A:B }; };
template<int N> struct absent {};
template<int Index,class T> struct choice { enum { index = Index }; typedef T type; };

/* The shapes need exact types, promotions, char* -> string, and nullptr
 * -> shared_ptr. Filter arithmetic/pointer narrowing before overload selection,
 * so bool promotes to int rather than selecting a first-convertible float. */
template<class From,class To> struct nonnarrowing {
    typedef typename decay<From>::type F;
    enum { value =
        std::is_same<F,To>::value ||
        (!std::is_arithmetic<To>::value) ||
        (std::is_integral<F>::value && std::is_integral<To>::value &&
         std::numeric_limits<To>::digits >= std::numeric_limits<F>::digits &&
         (!std::numeric_limits<F>::is_signed || std::numeric_limits<To>::is_signed)) ||
        (std::is_floating_point<F>::value && std::is_floating_point<To>::value &&
         std::numeric_limits<To>::digits >= std::numeric_limits<F>::digits &&
         std::numeric_limits<To>::max_exponent >= std::numeric_limits<F>::max_exponent)
    };
};
template<class T,class U> struct nothrow_conversion {
#if defined(_MSC_VER) && _MSC_VER < 1900
    enum { value = std::is_same<T,typename decay<U>::type>::value &&
        (std::is_lvalue_reference<U>::value ? is_nothrow_copy<T>::value : is_nothrow_move<T>::value) };
#else
    enum { value = std::is_nothrow_constructible<T,U&&>::value };
#endif
};
}
}
#endif
