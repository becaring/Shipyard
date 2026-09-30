#ifndef SHIPYARD_JSON_HPP
#define SHIPYARD_JSON_HPP
// Xbox compiler-library implementation of the reached nlohmann 3.12.0 API.
// Lexer and Grisu2 algorithms are generated from pinned official nlohmann source.
#include <map>
#include <array>
#include <vector>
#include <string>
#include <memory>
#include <stdexcept>
#include <limits>
#include <istream>
#include <ostream>
#include <utility>
#include <cstddef>
#include <type_traits>
#include <tuple>

namespace nlohmann {
class json;
class StreamWriter;
namespace detail {
struct value_t {
    enum type {
        null,
        object,
        array,
        string,
        boolean,
        number_integer,
        number_unsigned,
        number_float,
        binary,
        discarded
    };
};
template <class T> class iter_impl;
template <class Iterator> class iteration_proxy;
template <class T, class Enable = void>
struct conversion; // Unknown conversions deliberately fail compilation.
// XDK has is_enum but no underlying_type intrinsic. The compiler projection must
// register the exact underlying type after declaring each reached enum.
#if defined(_XBOX)
template <class T> struct enum_underlying;
#else
template <class T> struct enum_underlying : std::underlying_type<T> {};
#endif
} // namespace detail
class json {
public:
    typedef long long number_integer_t;
    typedef unsigned long long number_unsigned_t;
    typedef double number_float_t;
    typedef std::string string_t;
    typedef std::map<std::string, json> object_t;
    typedef std::vector<json> array_t;
    typedef detail::iter_impl<json> iterator;
    typedef detail::iter_impl<const json> const_iterator;
    typedef detail::value_t::type value_t;
    class exception : public std::runtime_error {
    public:
        const int id;
        exception(int i, const std::string& text) : std::runtime_error(text), id(i) {
        }
        virtual ~exception() throw() {
        }
    };
    class type_error : public exception {
    public:
        type_error(int i, const std::string& text);
    };
    class out_of_range : public exception {
    public:
        out_of_range(int i, const std::string& text);
    };
    class invalid_iterator : public exception {
    public:
        invalid_iterator(int i, const std::string& text);
    };
    class parse_error : public exception {
    public:
        const size_t byte;
        parse_error(int i, size_t b, const std::string& text);
    };
    class json_pointer {
    public:
        explicit json_pointer(const std::string& text = "");
        std::string to_string() const;

    private:
        friend class json;
        std::vector<std::string> tokens;
    };
    json() : type_(detail::value_t::null), signed_(0), unsigned_(0), real_(0) {
    }
    json(std::nullptr_t) : type_(detail::value_t::null), signed_(0), unsigned_(0), real_(0) {
    }
    json(const json&);
    json(json&&);
    json& operator=(const json&);
    json& operator=(json&&);
    ~json();
    void swap(json&);
    json(const char* v)
        : type_(detail::value_t::string), text_(v), signed_(0), unsigned_(0), real_(0) {
    }
    json(const std::string& v)
        : type_(detail::value_t::string), text_(v), signed_(0), unsigned_(0), real_(0) {
    }
    json(bool v) : type_(detail::value_t::boolean), signed_(v ? 1 : 0), unsigned_(0), real_(0) {
    }
    json(char v) : type_(detail::value_t::number_integer), signed_(v), unsigned_(0), real_(0) {
    }
    json(signed char v)
        : type_(detail::value_t::number_integer), signed_(v), unsigned_(0), real_(0) {
    }
    json(unsigned char v)
        : type_(detail::value_t::number_unsigned), signed_(0), unsigned_(v), real_(0) {
    }
    json(short v) : type_(detail::value_t::number_integer), signed_(v), unsigned_(0), real_(0) {
    }
    json(unsigned short v)
        : type_(detail::value_t::number_unsigned), signed_(0), unsigned_(v), real_(0) {
    }
    json(int v) : type_(detail::value_t::number_integer), signed_(v), unsigned_(0), real_(0) {
    }
    json(unsigned int v)
        : type_(detail::value_t::number_unsigned), signed_(0), unsigned_(v), real_(0) {
    }
    json(long v) : type_(detail::value_t::number_integer), signed_(v), unsigned_(0), real_(0) {
    }
    json(unsigned long v)
        : type_(detail::value_t::number_unsigned), signed_(0), unsigned_(v), real_(0) {
    }
    json(long long v) : type_(detail::value_t::number_integer), signed_(v), unsigned_(0), real_(0) {
    }
    json(unsigned long long v)
        : type_(detail::value_t::number_unsigned), signed_(0), unsigned_(v), real_(0) {
    }
    json(float v) : type_(detail::value_t::number_float), signed_(0), unsigned_(0), real_(v) {
    }
    json(double v) : type_(detail::value_t::number_float), signed_(0), unsigned_(0), real_(v) {
    }
    template <class T, class A>
    json(const std::vector<T, A>& v)
        : type_(detail::value_t::null), signed_(0), unsigned_(0), real_(0) {
        *this = array();
        for (size_t i = 0; i < v.size(); ++i)
            push_back(json(v[i]));
    }
    template <class T, class C, class A>
    json(const std::map<std::string, T, C, A>& v)
        : type_(detail::value_t::null), signed_(0), unsigned_(0), real_(0) {
        *this = object();
        for (typename std::map<std::string, T, C, A>::const_iterator i = v.begin(); i != v.end();
             ++i)
            (*this)[i->first] = json(i->second);
    }
    template <class T, size_t N>
    json(const std::array<T, N>& v)
        : type_(detail::value_t::null), signed_(0), unsigned_(0), real_(0) {
        *this = array();
        for (size_t i = 0; i < N; ++i)
            push_back(json(v[i]));
    }
    template <class T, size_t N>
    json(const T (&v)[N], typename std::enable_if<!std::is_same<T, char>::value>::type* = 0)
        : type_(detail::value_t::null), signed_(0), unsigned_(0), real_(0) {
        *this = array();
        for (size_t i = 0; i < N; ++i)
            push_back(json(v[i]));
    }
    template <class T>
    json(T v, typename std::enable_if<std::is_enum<T>::value>::type* = 0)
        : type_(detail::value_t::null), signed_(0), unsigned_(0), real_(0) {
        *this = json(static_cast<typename detail::enum_underlying<T>::type>(v));
    }
    template <class T> T get() const {
        return detail::conversion<T>::get(*this);
    }
    template <class T> T& get_to(T& v) const {
        v = get<T>();
        return v;
    }
    template <class T, size_t N> T (&get_to(T (&v)[N]) const)[N] {
        for (size_t i = 0; i < N; ++i)
            at(i).get_to(v[i]);
        return v;
    }
    template <class T, size_t N> std::array<T, N>& get_to(std::array<T, N>& v) const {
        if (!is_array())
            throw type_error(302, std::string("type must be array, but is ") + type_name());
        for (size_t i = 0; i < N; ++i)
            v[i] = at(i).get<T>();
        return v;
    }
    template <class T, class C, class A> operator std::map<std::string, T, C, A>() const {
        return get<std::map<std::string, T, C, A> >();
    }
    template <class T, size_t N> operator std::array<T, N>() const {
        return get<std::array<T, N> >();
    }
    operator std::string() const;
    operator bool() const;
    operator int() const;
    operator unsigned int() const;
    operator float() const;
    operator double() const;
    operator signed char() const;
    operator unsigned char() const;
    operator short() const;
    operator unsigned short() const;
    operator long() const;
    operator unsigned long() const;
    operator long long() const;
    operator unsigned long long() const;

    static json object();
    static json array();
    // Only the null callback form of the reached nlohmann parse API is supported.
    static json parse(const std::string& text, std::nullptr_t callback = nullptr,
                      bool allow_exceptions = true, bool ignore_comments = false);
    static json parse(const char* text, std::nullptr_t callback = nullptr,
                      bool allow_exceptions = true, bool ignore_comments = false) {
        return parse(std::string(text), callback, allow_exceptions, ignore_comments);
    }
    static json parse(std::istream& stream, std::nullptr_t callback = nullptr,
                      bool allow_exceptions = true, bool ignore_comments = false);
    friend std::istream& operator>>(std::istream&, json&);
    friend std::ostream& operator<<(std::ostream&, const json&);
    friend class StreamWriter;
    template <class Iterator> static json parse(Iterator begin, Iterator end) {
        return parse(std::string(begin, end));
    }
    std::string dump(int indent = -1, char indent_char = ' ', bool ensure_ascii = false) const;
    value_t type() const {
        return type_;
    }
    const char* type_name() const;
    bool is_null() const {
        return type_ == detail::value_t::null;
    }
    bool is_object() const {
        return type_ == detail::value_t::object;
    }
    bool is_array() const {
        return type_ == detail::value_t::array;
    }
    bool is_string() const {
        return type_ == detail::value_t::string;
    }
    bool is_boolean() const {
        return type_ == detail::value_t::boolean;
    }
    bool is_number_integer() const {
        return type_ == detail::value_t::number_integer ||
               type_ == detail::value_t::number_unsigned;
    }
    bool is_number_unsigned() const {
        return type_ == detail::value_t::number_unsigned;
    }
    bool is_number_float() const {
        return type_ == detail::value_t::number_float;
    }
    bool is_primitive() const {
        return !is_object() && !is_array() && !is_discarded();
    }
    bool is_discarded() const {
        return type_ == detail::value_t::discarded;
    }
    bool is_structured() const {
        return is_object() || is_array();
    }
    bool is_number() const {
        return is_number_integer() || is_number_float();
    }
    template <class T> T value(const std::string& key, const T& fallback) const {
        if (!is_object())
            throw type_error(306, std::string("cannot use value() with ") + type_name());
        const json* found = FindValue(key);
        return found ? found->get<T>() : fallback;
    }
    std::string value(const std::string& key, const char* fallback) const {
        if (!is_object())
            throw type_error(306, std::string("cannot use value() with ") + type_name());
        const json* found = FindValue(key);
        return found ? found->get<std::string>() : std::string(fallback);
    }
    iterator find(const std::string&);
    const_iterator find(const std::string&) const;
    iterator erase(iterator);
    void update(const json&, bool merge_objects = false);
    bool contains(const std::string&) const;
    size_t size() const;
    bool empty() const {
        return size() == 0;
    }
    json& operator[](const std::string&);
    const json& operator[](const std::string&) const;
    json& operator[](const char* s) {
        return (*this)[std::string(s)];
    }
    const json& operator[](const char* s) const {
        return (*this)[std::string(s)];
    }
    json& operator[](size_t);
    const json& operator[](size_t) const;
    json& operator[](int n) {
        return (*this)[static_cast<size_t>(n)];
    }
    const json& operator[](int n) const {
        return (*this)[static_cast<size_t>(n)];
    }
    json& at(const std::string&);
    const json& at(const std::string&) const;
    json& at(size_t);
    const json& at(size_t) const;
    json& operator[](const json_pointer&);
    const json& operator[](const json_pointer&) const;
    json& at(const json_pointer&);
    const json& at(const json_pointer&) const;
    void clear();
    size_t erase(const std::string&);
    std::pair<iterator, bool> emplace(const std::string&, const json&);
    void push_back(const json&);
    void push_back(json&&);
    iterator begin();
    iterator end();
    const_iterator begin() const;
    const_iterator end() const;
    detail::iteration_proxy<iterator> items();
    detail::iteration_proxy<const_iterator> items() const;
    json flatten() const;
    json unflatten() const;
    bool operator==(const json&) const;
    bool operator!=(const json& r) const;
    bool operator==(const char* value) const {
        return *this == json(value);
    }
    bool operator!=(const char* value) const {
        return *this != json(value);
    }
    bool operator==(const std::string& value) const {
        return *this == json(value);
    }
    bool operator!=(const std::string& value) const {
        return *this != json(value);
    }
    bool operator==(int value) const {
        return *this == json(value);
    }
    bool operator!=(int value) const {
        return *this != json(value);
    }
    bool operator==(unsigned int value) const {
        return *this == json(value);
    }
    bool operator!=(unsigned int value) const {
        return *this != json(value);
    }
    bool operator==(double value) const {
        return *this == json(value);
    }
    bool operator!=(double value) const {
        return *this != json(value);
    }
    bool operator==(bool value) const {
        return *this == json(value);
    }
    bool operator!=(bool value) const {
        return *this != json(value);
    }

private:
    const json* FindValue(const std::string&) const;
    template <class T> friend class detail::iter_impl;
    template <class T, class Enable> friend struct detail::conversion;
    struct ObjectStorage;
    struct ArrayStorage;
    value_t type_;
    std::unique_ptr<ObjectStorage> object_;
    std::unique_ptr<ArrayStorage> array_;
    std::string text_;
    long long signed_;
    unsigned long long unsigned_;
    double real_;
    void append(std::string&, int, char, bool, unsigned) const;
    void flatten_into(const std::string&, json&) const;
    json& pointer_get(const json_pointer&, bool checked, bool unflatten);
};
struct json::ObjectStorage : json::object_t {};
struct json::ArrayStorage : json::array_t {};
namespace detail {
#define SHIPYARD_JSON_GET(T)                                                                       \
    template <> struct conversion<T> {                                                             \
        static T get(const json& j) {                                                              \
            switch (j.type_) {                                                                     \
            case value_t::number_integer:                                                          \
                return static_cast<T>(j.signed_);                                                  \
            case value_t::number_unsigned:                                                         \
                return static_cast<T>(j.unsigned_);                                                \
            case value_t::number_float:                                                            \
                return static_cast<T>(j.real_);                                                    \
            case value_t::boolean:                                                                 \
                if (!std::is_same<T, json::number_integer_t>::value &&                             \
                    !std::is_same<T, json::number_unsigned_t>::value &&                            \
                    !std::is_same<T, json::number_float_t>::value)                                 \
                    return static_cast<T>(j.signed_);                                              \
            default:                                                                               \
                throw json::type_error(302, std::string("type must be number, but is ") +          \
                                                j.type_name());                                    \
            }                                                                                      \
        }                                                                                          \
    }
SHIPYARD_JSON_GET(char);
SHIPYARD_JSON_GET(signed char);
SHIPYARD_JSON_GET(unsigned char);
SHIPYARD_JSON_GET(short);
SHIPYARD_JSON_GET(unsigned short);
SHIPYARD_JSON_GET(int);
SHIPYARD_JSON_GET(unsigned int);
SHIPYARD_JSON_GET(long);
SHIPYARD_JSON_GET(unsigned long);
SHIPYARD_JSON_GET(long long);
SHIPYARD_JSON_GET(unsigned long long);
SHIPYARD_JSON_GET(float);
SHIPYARD_JSON_GET(double);
#undef SHIPYARD_JSON_GET
template <> struct conversion<std::string> {
    static std::string get(const json& j) {
        if (!j.is_string())
            throw json::type_error(302,
                                   std::string("type must be string, but is ") + j.type_name());
        return j.text_;
    }
};
template <> struct conversion<bool> {
    static bool get(const json& j) {
        if (!j.is_boolean())
            throw json::type_error(302,
                                   std::string("type must be boolean, but is ") + j.type_name());
        return j.signed_ != 0;
    }
};
template <> struct conversion<json> {
    static json get(const json& j) {
        return j;
    }
};
template <class T> struct conversion<T, typename std::enable_if<std::is_enum<T>::value>::type> {
    static T get(const json& j) {
        // Enum conversion uses upstream get_arithmetic_value, which rejects bool
        // even when the enum's underlying arithmetic type accepts it directly.
        if (!j.is_number())
            throw json::type_error(302,
                                   std::string("type must be number, but is ") + j.type_name());
        return static_cast<T>(j.get<typename enum_underlying<T>::type>());
    }
};
template <class T, class C, class A> struct conversion<std::map<std::string, T, C, A> > {
    static std::map<std::string, T, C, A> get(const json& j) {
        if (!j.is_object())
            throw json::type_error(302,
                                   std::string("type must be object, but is ") + j.type_name());
        std::map<std::string, T, C, A> result;
        for (json::object_t::const_iterator i = j.object_->begin(); i != j.object_->end(); ++i)
            result.insert(std::make_pair(i->first, i->second.get<T>()));
        return result;
    }
};
template <class T, size_t N> struct conversion<std::array<T, N> > {
    static std::array<T, N> get(const json& j) {
        std::array<T, N> result;
        j.get_to(result);
        return result;
    }
};
template <class T, class A> struct conversion<std::vector<T, A> > {
    static std::vector<T, A> get(const json& j) {
        if (!j.is_array())
            throw json::type_error(302, std::string("type must be array, but is ") + j.type_name());
        std::vector<T, A> result;
        result.reserve(j.size());
        for (size_t i = 0; i < j.size(); ++i)
            result.push_back(j[i].get<T>());
        return result;
    }
};

template <class T> class iter_impl {
public:
    typedef T& reference;
    typedef T* pointer;
    iter_impl() : owner_(0), index_(0) {
    }
    reference operator*() const {
        return value();
    }
    pointer operator->() const {
        return &value();
    }
    reference value() const {
        if (owner_->is_object())
            return object_->second;
        if (owner_->is_array())
            return (*owner_->array_)[index_];
        return *owner_;
    }
    const std::string& key() const {
        if (owner_->is_object())
            return object_->first;
        throw json::invalid_iterator(207, "cannot use key() for non-object iterators");
    }
    iter_impl& operator++() {
        if (owner_->is_object())
            ++object_;
        else
            ++index_;
        return *this;
    }
    iter_impl operator++(int) {
        iter_impl old = *this;
        ++*this;
        return old;
    }
    bool operator==(const iter_impl& r) const {
        return owner_ == r.owner_ &&
               (!owner_ || (owner_->is_object() ? object_ == r.object_ : index_ == r.index_));
    }
    bool operator!=(const iter_impl& r) const {
        return !(*this == r);
    }
    T* owner() const {
        return owner_;
    }
    size_t index() const {
        return index_;
    }

private:
    friend class nlohmann::json;
    T* owner_;
    size_t index_;
    typename std::conditional<std::is_const<T>::value, json::object_t::const_iterator,
                              json::object_t::iterator>::type object_;
};
std::string index_string(size_t);
template <class Iterator> class iteration_proxy_value {
public:
    explicit iteration_proxy_value(Iterator i)
        : it(i), array_index(0), array_index_last(0), array_index_str("0") {
    }
    iteration_proxy_value& operator++() {
        ++it;
        ++array_index;
        return *this;
    }
    iteration_proxy_value operator++(int) {
        iteration_proxy_value old = *this;
        ++*this;
        return old;
    }
    bool operator==(const iteration_proxy_value& r) const {
        return it == r.it;
    }
    bool operator!=(const iteration_proxy_value& r) const {
        return it != r.it;
    }
    const iteration_proxy_value& operator*() const {
        return *this;
    }
    typename Iterator::reference value() const {
        return it.value();
    }
    const std::string& key() const {
        if (it.owner()->is_object())
            return it.key();
        if (it.owner()->is_array()) {
            if (array_index_last != array_index) {
                array_index_str = index_string(array_index);
                array_index_last = array_index;
            }
            return array_index_str;
        }
        return empty_str;
    }

private:
    Iterator it;
    size_t array_index;
    mutable size_t array_index_last;
    mutable std::string array_index_str;
    std::string empty_str;
};
template <class Iterator> class iteration_proxy {
public:
    explicit iteration_proxy(typename Iterator::reference c) : container(&c) {
    }
    iteration_proxy_value<Iterator> begin() const {
        return iteration_proxy_value<Iterator>(container->begin());
    }
    iteration_proxy_value<Iterator> end() const {
        return iteration_proxy_value<Iterator>(container->end());
    }

private:
    typename Iterator::pointer container;
};
template <size_t N, class Iterator> struct proxy_get;
template <class Iterator> struct proxy_get<0, Iterator> {
    typedef const std::string& type;
    static type value(const iteration_proxy_value<Iterator>& i) {
        return i.key();
    }
};
template <class Iterator> struct proxy_get<1, Iterator> {
    typedef typename Iterator::reference type;
    static type value(const iteration_proxy_value<Iterator>& i) {
        return i.value();
    }
};
template <size_t N, class Iterator>
typename proxy_get<N, Iterator>::type get(const iteration_proxy_value<Iterator>& i) {
    return proxy_get<N, Iterator>::value(i);
}

} // namespace detail
inline json::operator std::string() const {
    return get<std::string>();
}
inline json::operator bool() const {
    return get<bool>();
}
inline json::operator int() const {
    return get<int>();
}
inline json::operator unsigned int() const {
    return get<unsigned int>();
}
inline json::operator float() const {
    return get<float>();
}
inline json::operator double() const {
    return get<double>();
}
inline json::operator signed char() const {
    return get<signed char>();
}
inline json::operator unsigned char() const {
    return get<unsigned char>();
}
inline json::operator short() const {
    return get<short>();
}
inline json::operator unsigned short() const {
    return get<unsigned short>();
}
inline json::operator long() const {
    return get<long>();
}
inline json::operator unsigned long() const {
    return get<unsigned long>();
}
inline json::operator long long() const {
    return get<long long>();
}
inline json::operator unsigned long long() const {
    return get<unsigned long long>();
}

} // namespace nlohmann
namespace std {
template <class Iterator>
struct tuple_size<nlohmann::detail::iteration_proxy_value<Iterator> >
    : integral_constant<size_t, 2> {};
template <size_t N, class Iterator>
struct tuple_element<N, nlohmann::detail::iteration_proxy_value<Iterator> > {
    typedef typename nlohmann::detail::proxy_get<N, Iterator>::type type;
};
} // namespace std
#include "json/generated/ordered_json.hpp"
#endif
