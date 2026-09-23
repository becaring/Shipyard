#ifndef SHIPYARD_JSON_ALGORITHM_SUPPORT_H
#define SHIPYARD_JSON_ALGORITHM_SUPPORT_H
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <istream>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#define NLOHMANN_JSON_NAMESPACE_BEGIN namespace ShipyardJsonAlgorithm {
#define NLOHMANN_JSON_NAMESPACE_END }
#define JSON_ASSERT(x) assert(x)
#define JSON_HEDLEY_RETURNS_NON_NULL
#define JSON_HEDLEY_CONST
#define JSON_HEDLEY_PURE
#define JSON_HEDLEY_NON_NULL(...)
#define JSON_HEDLEY_LIKELY(x) (x)
#define JSON_HEDLEY_UNLIKELY(x) (x)
namespace ShipyardJsonAlgorithm {
inline bool finite(double value) { return _finite(value) != 0; }
inline bool signbit(double value) {
    unsigned long long bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits >> 63) != 0;
}
namespace detail {
template <class T> struct char_traits : std::char_traits<T> {};
struct position_t {
    size_t chars_read_total, chars_read_current_line, lines_read;
    position_t() : chars_read_total(0), chars_read_current_line(0), lines_read(0) {}
    operator size_t() const { return chars_read_total; }
};
struct ByteRanges {
    int data[6];
    size_t count;
    ByteRanges(int a, int b) : count(2) {
        data[0] = a;
        data[1] = b;
    }
    ByteRanges(int a, int b, int c, int d) : count(4) {
        data[0] = a;
        data[1] = b;
        data[2] = c;
        data[3] = d;
    }
    ByteRanges(int a, int b, int c, int d, int e, int f) : count(6) {
        data[0] = a;
        data[1] = b;
        data[2] = c;
        data[3] = d;
        data[4] = e;
        data[5] = f;
    }
    size_t size() const { return count; }
    const int *begin() const { return data; }
    const int *end() const { return data + count; }
};
struct StringInput {
    typedef char char_type;
    const std::string *source;
    size_t position;
    std::istream *stream;
    std::streambuf *buffer;
    explicit StringInput(const std::string &text) : source(&text), position(0), stream(0), buffer(0) {}
    explicit StringInput(std::istream &input) : source(0), position(0), stream(&input), buffer(input.rdbuf()) {}
    int get_character() {
        // Pinned input_stream_adapter bypasses sentry, skipws and fail/bad bits.
        if (stream) {
            int result = buffer->sbumpc();
            if (result == std::char_traits<char>::eof())
                stream->clear(stream->rdstate() | std::ios::eofbit);
            return result;
        }
        return position == source->size() ? std::char_traits<char>::eof()
                                          : static_cast<unsigned char>((*source)[position++]);
    }
};
} // namespace detail
} // namespace ShipyardJsonAlgorithm
#endif
