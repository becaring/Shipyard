#include <nlohmann/json.hpp>
#include "json/generated/lexer.hpp"
#include "json/generated/to_chars.hpp"
#include <sstream>
#include <iomanip>
#include <cassert>
#include <cerrno>
#include <algorithm>

namespace nlohmann {
namespace detail {
std::string index_string(size_t v) {
    std::ostringstream s;
    s << v;
    return s.str();
}
} // namespace detail
static std::string Prefix(const char *kind, int id) {
    return std::string("[json.exception.") + kind + "." + detail::index_string(id) + "] ";
}
json::type_error::type_error(int i, const std::string &s) : exception(i, Prefix("type_error", i) + s) {}
json::out_of_range::out_of_range(int i, const std::string &s) : exception(i, Prefix("out_of_range", i) + s) {}
json::invalid_iterator::invalid_iterator(int i, const std::string &s)
    : exception(i, Prefix("invalid_iterator", i) + s) {}
json::parse_error::parse_error(int i, size_t b, const std::string &s)
    : exception(i, Prefix("parse_error", i) + s), byte(b) {}
json::json(const json &o)
    : type_(o.type_), object_(o.object_.get() ? new ObjectStorage(*o.object_) : 0),
      array_(o.array_.get() ? new ArrayStorage(*o.array_) : 0), text_(o.text_), signed_(o.signed_),
      unsigned_(o.unsigned_), real_(o.real_) {}
json::json(json &&o)
    : type_(o.type_), object_(std::move(o.object_)), array_(std::move(o.array_)), text_(std::move(o.text_)),
      signed_(o.signed_), unsigned_(o.unsigned_), real_(o.real_) {
    o.type_ = detail::value_t::null;
}
json &json::operator=(const json &o) {
    if (this != &o) {
        json copy(o);
        swap(copy);
    }
    return *this;
}
json &json::operator=(json &&o) {
    if (this != &o) {
        json copy(std::move(o));
        swap(copy);
    }
    return *this;
}
// Same heap-stack destruction strategy as pinned json.hpp::json_value::destroy;
// moved children are null before their owning containers are released.
json::~json() {
    if ((is_array() && array_.get() && !array_->empty()) ||
        (is_object() && object_.get() && !object_->empty())) {
        std::vector<json> stack;
        stack.reserve(size());
        if (is_array()) {
            for (size_t i = 0; i < array_->size(); ++i)
                stack.push_back(std::move((*array_)[i]));
            array_->clear();
        } else {
            for (object_t::iterator i = object_->begin(); i != object_->end(); ++i)
                stack.push_back(std::move(i->second));
            object_->clear();
        }
        while (!stack.empty()) {
            json current(std::move(stack.back()));
            stack.pop_back();
            if (current.is_array()) {
                for (size_t i = 0; i < current.array_->size(); ++i)
                    stack.push_back(std::move((*current.array_)[i]));
                current.array_->clear();
            } else if (current.is_object()) {
                for (object_t::iterator i = current.object_->begin(); i != current.object_->end(); ++i)
                    stack.push_back(std::move(i->second));
                current.object_->clear();
            }
        }
    }
}
void json::swap(json &o) {
    std::swap(type_, o.type_);
    object_.swap(o.object_);
    array_.swap(o.array_);
    text_.swap(o.text_);
    std::swap(signed_, o.signed_);
    std::swap(unsigned_, o.unsigned_);
    std::swap(real_, o.real_);
}
json json::object() {
    json j;
    j.object_.reset(new ObjectStorage());
    j.type_ = detail::value_t::object;
    return j;
}
json json::array() {
    json j;
    j.array_.reset(new ArrayStorage());
    j.type_ = detail::value_t::array;
    return j;
}
const char *json::type_name() const {
    static const char *names[] = {"null",   "object", "array",  "string", "boolean",
                                  "number", "number", "number", "binary", "discarded"};
    return names[type_];
}
bool json::contains(const std::string &k) const { return is_object() && object_->find(k) != object_->end(); }
size_t json::size() const {
    return is_object() ? object_->size() : is_array() ? array_->size() : is_null() ? 0 : 1;
}
json &json::operator[](const std::string &k) {
    if (is_null())
        *this = object();
    if (!is_object())
        throw type_error(305,
                         std::string("cannot use operator[] with a string argument with ") + type_name());
    return (*object_)[k];
}
const json &json::operator[](const std::string &k) const {
    if (!is_object())
        throw type_error(305,
                         std::string("cannot use operator[] with a string argument with ") + type_name());
    object_t::const_iterator i = object_->find(k);
    assert(i != object_->end());
    return i->second;
}
json &json::operator[](size_t n) {
    if (is_null())
        *this = array();
    if (!is_array())
        throw type_error(305,
                         std::string("cannot use operator[] with a numeric argument with ") + type_name());
    if (n >= array_->size())
        array_->resize(n + 1);
    return (*array_)[n];
}
const json &json::operator[](size_t n) const {
    if (!is_array())
        throw type_error(305,
                         std::string("cannot use operator[] with a numeric argument with ") + type_name());
    assert(n < array_->size());
    return (*array_)[n];
}
json &json::at(const std::string &k) { return const_cast<json &>(static_cast<const json *>(this)->at(k)); }
const json &json::at(const std::string &k) const {
    if (!is_object())
        throw type_error(304, std::string("cannot use at() with ") + type_name());
    object_t::const_iterator i = object_->find(k);
    if (i == object_->end())
        throw out_of_range(403, "key '" + k + "' not found");
    return i->second;
}
json &json::at(size_t n) { return const_cast<json &>(static_cast<const json *>(this)->at(n)); }
const json &json::at(size_t n) const {
    if (!is_array())
        throw type_error(304, std::string("cannot use at() with ") + type_name());
    if (n >= size())
        throw out_of_range(401, "array index " + detail::index_string(n) + " is out of range");
    return (*array_)[n];
}
void json::clear() {
    if (is_object())
        object_->clear();
    else if (is_array())
        array_->clear();
    else if (is_string())
        text_.clear();
    else {
        signed_ = 0;
        unsigned_ = 0;
        real_ = 0;
    }
}
size_t json::erase(const std::string &k) {
    if (!is_object())
        throw type_error(307, std::string("cannot use erase() with ") + type_name());
    return object_->erase(k);
}
std::pair<json::iterator, bool> json::emplace(const std::string &k, const json &v) {
    if (is_null())
        *this = object();
    if (!is_object())
        throw type_error(311, std::string("cannot use emplace() with ") + type_name());
    std::pair<object_t::iterator, bool> result = object_->insert(std::make_pair(k, v));
    iterator i;
    i.owner_ = this;
    i.object_ = result.first;
    return std::make_pair(i, result.second);
}
void json::push_back(const json &v) {
    if (is_null())
        *this = array();
    if (!is_array())
        throw type_error(308, std::string("cannot use push_back() with ") + type_name());
    array_->push_back(v);
}
void json::push_back(json &&v) {
    if (is_null())
        *this = array();
    if (!is_array())
        throw type_error(308, std::string("cannot use push_back() with ") + type_name());
    array_->push_back(std::move(v));
}
json::iterator json::begin() {
    iterator i;
    i.owner_ = this;
    if (is_object())
        i.object_ = object_->begin();
    return i;
}
json::iterator json::end() {
    iterator i;
    i.owner_ = this;
    i.index_ = size();
    if (is_object())
        i.object_ = object_->end();
    return i;
}
json::const_iterator json::begin() const {
    const_iterator i;
    i.owner_ = this;
    if (is_object())
        i.object_ = object_->begin();
    return i;
}
json::const_iterator json::end() const {
    const_iterator i;
    i.owner_ = this;
    i.index_ = size();
    if (is_object())
        i.object_ = object_->end();
    return i;
}
// Reached modern SoH lookup and erase contracts follow pinned json.hpp.
void json::update(const json &other, bool merge_objects) {
    if (is_null()) *this=object();
    if (!is_object()) throw type_error(312, std::string("cannot use update() with ")+type_name());
    if (!other.is_object()) throw type_error(312, std::string("cannot use update() with ")+other.type_name());
    for (const_iterator it=other.begin();it!=other.end();++it) {
        if (merge_objects && it.value().is_object()) {
            object_t::iterator found=object_->find(it.key());
            if (found!=object_->end()) { found->second.update(it.value(),true); continue; }
        }
        (*object_)[it.key()]=it.value();
    }
}
json::iterator json::find(const std::string &key) {
    iterator result=end();
    if (is_object()) result.object_=object_->find(key);
    return result;
}
json::const_iterator json::find(const std::string &key) const {
    const_iterator result=end();
    if (is_object()) result.object_=object_->find(key);
    return result;
}
json::iterator json::erase(iterator pos) {
    if (pos.owner_ != this) throw invalid_iterator(202, "iterator does not fit current value");
    iterator result=end();
    if (is_object()) {
        result=pos; ++result; object_->erase(pos.object_);
    } else if (is_array()) {
        array_->erase(array_->begin()+pos.index_); result.index_=pos.index_;
    } else if (is_null() || is_discarded()) {
        throw type_error(307, std::string("cannot use erase() with ")+type_name());
    } else {
        if (pos.index_ != 0) throw invalid_iterator(205, "iterator out of range");
        *this=json();
    }
    return result;
}
detail::iteration_proxy<json::iterator> json::items() { return detail::iteration_proxy<iterator>(*this); }
detail::iteration_proxy<json::const_iterator> json::items() const {
    return detail::iteration_proxy<const_iterator>(*this);
}

// Iterative DOM parser driven by the actual 3.12.0 scanner. Error construction
// follows parser.hpp::exception_message and exceptions.hpp::parse_error.
class Parser {
    typedef ShipyardJsonAlgorithm::detail::lexer<json, ShipyardJsonAlgorithm::detail::StringInput> Lexer;
    typedef Lexer::token_scope Tok;
    typedef Lexer::token_type Token;
    Lexer lexer;
    Token token;
    bool strict_scanned;
    struct Frame {
        json *value;
        int state;
        std::string key;
        Frame(json *v) : value(v), state(0) {}
    };
    void next(bool final_scan = false) { if (final_scan) strict_scanned = true; token = lexer.scan(); }
    void fail(Token expected, const std::string &context, const std::string &overrideText = "") {
        ShipyardJsonAlgorithm::detail::position_t p = lexer.get_position();
        std::string message = overrideText;
        if (message.empty()) {
            message = "syntax error while parsing " + context + " - ";
            if (token == Tok::parse_error)
                message += std::string(lexer.get_error_message()) + "; last read: '" +
                           lexer.get_token_string() + "'";
            else
                message += std::string("unexpected ") + Lexer::token_type_name(token);
            if (expected != Tok::uninitialized)
                message += std::string("; expected ") + Lexer::token_type_name(expected);
        }
        throw json::parse_error(101, p.chars_read_total,
                                "parse error at line " + detail::index_string(p.lines_read + 1) +
                                    ", column " + detail::index_string(p.chars_read_current_line) + ": " +
                                    message);
    }
    bool value(json &target) {
        switch (token) {
        case Tok::begin_object:
            target = json::object();
            return true;
        case Tok::begin_array:
            target = json::array();
            return true;
        case Tok::literal_null:
            target = json();
            break;
        case Tok::literal_true:
            target = true;
            break;
        case Tok::literal_false:
            target = false;
            break;
        case Tok::value_string:
            target = lexer.get_string();
            break;
        case Tok::value_integer:
            target = lexer.get_number_integer();
            break;
        case Tok::value_unsigned:
            target = lexer.get_number_unsigned();
            break;
        case Tok::value_float:
            if (!ShipyardJsonAlgorithm::finite(lexer.get_number_float()))
                throw json::out_of_range(406, "number overflow parsing '" + lexer.get_token_string() + "'");
            target = lexer.get_number_float();
            break;
        case Tok::parse_error:
            fail(Tok::uninitialized, "value");
            break;
        case Tok::end_of_input:
            if (lexer.get_position().chars_read_total == 1)
                fail(Tok::uninitialized, "value",
                     "attempting to parse an empty input; check that your input string or stream contains "
                     "the expected JSON");
        default:
            fail(Tok::literal_or_value, "value");
        }
        return false;
    }

  public:
    Parser(const std::string &text, bool comments)
        : lexer(ShipyardJsonAlgorithm::detail::StringInput(text), comments), token(Tok::uninitialized), strict_scanned(false) {}
    Parser(std::istream &stream, bool comments)
        : lexer(ShipyardJsonAlgorithm::detail::StringInput(stream), comments), token(Tok::uninitialized), strict_scanned(false) {}
    void consume_strict_after_error() { if (!strict_scanned) next(); }
    void parse(json &root, bool strict) {
        next();
        std::vector<Frame> stack;
        if (value(root))
            stack.push_back(Frame(&root));
        if (!stack.empty() || strict) next(stack.empty());
        while (!stack.empty()) {
            Frame &f = stack.back();
            json *target = 0;
            if (f.value->is_object()) {
                if (f.state == 0 || f.state == 4) {
                    if (f.state == 0 && token == Tok::end_object) {
                        stack.pop_back();
                        if (!stack.empty() || strict) next(stack.empty());
                        continue;
                    }
                    if (token != Tok::value_string)
                        fail(Tok::value_string, "object key");
                    f.key = lexer.get_string();
                    (*f.value)[f.key]; // SAX key event inserts null before checking the colon.
                    f.state = 1;
                    next();
                    continue;
                }
                if (f.state == 1) {
                    if (token != Tok::name_separator)
                        fail(Tok::name_separator, "object separator");
                    f.state = 2;
                    next();
                    continue;
                }
                if (f.state == 2) {
                    target = &(*f.value)[f.key];
                    f.state = 3;
                } else if (f.state == 3) {
                    if (token == Tok::end_object) {
                        stack.pop_back();
                        if (!stack.empty() || strict) next(stack.empty());
                        continue;
                    }
                    if (token != Tok::value_separator)
                        fail(Tok::end_object, "object");
                    f.state = 4;
                    next();
                    continue;
                }
            } else {
                if (f.state == 0 || f.state == 4) {
                    if (f.state == 0 && token == Tok::end_array) {
                        stack.pop_back();
                        if (!stack.empty() || strict) next(stack.empty());
                        continue;
                    }
                    json element;
                    const bool structured = value(element); // Invalid tokens do not append an array element.
                    f.value->push_back(element);
                    target = &(*f.value)[f.value->size() - 1];
                    f.state = 3;
                    if (structured) stack.push_back(Frame(target));
                    next();
                    continue;
                } else {
                    if (token == Tok::end_array) {
                        stack.pop_back();
                        if (!stack.empty() || strict) next(stack.empty());
                        continue;
                    }
                    if (token != Tok::value_separator)
                        fail(Tok::end_array, "array");
                    f.state = 4;
                    next();
                    continue;
                }
            }
            if (target && value(*target))
                stack.push_back(Frame(target));
            next();
        }
        if (strict && token != Tok::end_of_input)
            fail(Tok::end_of_input, "value");
    }
};
json json::parse(const std::string &text, std::nullptr_t, bool allow, bool comments) {
    try {
        json result;
        Parser(text, comments).parse(result, true);
        return result;
    } catch (const exception &) {
        if (allow)
            throw;
        json result;
        result.type_ = detail::value_t::discarded;
        return result;
    }
}
// Same lifetime cleanup as pinned input_stream_adapter; only eofbit survives.
// With eofbit exceptions enabled, upstream may terminate from this destructor too.
class StreamInputScope {
    std::istream &stream;
  public:
    explicit StreamInputScope(std::istream &s) : stream(s) {}
    ~StreamInputScope() { stream.clear(stream.rdstate() & std::ios::eofbit); }
};
json json::parse(std::istream &stream, std::nullptr_t, bool allow, bool comments) {
    StreamInputScope scope(stream);
    json result;
    Parser parser(stream, comments);
    try {
        parser.parse(result, true);
    } catch (const exception &) {
        if (allow) throw;
        // Upstream performs its strict trailing-token scan even after a suppressed SAX error.
        parser.consume_strict_after_error();
        result = json();
        result.type_ = detail::value_t::discarded;
    }
    return result;
}
std::istream &operator>>(std::istream &stream, json &result) {
    StreamInputScope scope(stream);
    Parser(stream, false).parse(result, false);
    return stream;
}

static std::string EscapePointer(const std::string &text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '~')
            out += "~0";
        else if (text[i] == '/')
            out += "~1";
        else
            out += text[i];
    }
    return out;
}
json::json_pointer::json_pointer(const std::string &s) {
    if (s.empty())
        return;
    if (s[0] != '/')
        throw parse_error(
            107, 1, "parse error at byte 1: JSON pointer must be empty or begin with '/' - was: '" + s + "'");
    size_t start = 1;
    while (true) {
        size_t slash = s.find('/', start);
        std::string raw = s.substr(start, slash - start), decoded;
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] != '~')
                decoded += raw[i];
            else {
                if (i + 1 == raw.size() || (raw[i + 1] != '0' && raw[i + 1] != '1'))
                    throw parse_error(108, 0,
                                      "parse error: escape character '~' must be followed with '0' or '1'");
                decoded += raw[++i] == '0' ? '~' : '/';
            }
        }
        tokens.push_back(decoded);
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }
}
std::string json::json_pointer::to_string() const {
    std::string result;
    for (size_t i = 0; i < tokens.size(); ++i)
        result += "/" + EscapePointer(tokens[i]);
    return result;
}
static size_t ArrayIndex(const std::string &s) {
    if (s.size() > 1 && s[0] == '0')
        throw json::parse_error(106, 0, "parse error: array index '" + s + "' must not begin with '0'");
    if (s.size() > 1 && !(s[0] >= '1' && s[0] <= '9'))
        throw json::parse_error(109, 0, "parse error: array index '" + s + "' is not a number");
    char *end = 0;
    errno = 0;
    unsigned long long result = _strtoui64(s.c_str(), &end, 10);
    if (s.empty() || end != s.c_str() + s.size() || errno == ERANGE)
        throw json::out_of_range(404, "unresolved reference token '" + s + "'");
    if (result > (std::numeric_limits<size_t>::max)())
        throw json::out_of_range(410, "array index " + s + " exceeds size_type");
    return static_cast<size_t>(result);
}
json &json::pointer_get(const json_pointer &p, bool checked, bool unflatten) {
    json *j = this;
    for (size_t i = 0; i < p.tokens.size(); ++i) {
        const std::string &token = p.tokens[i];
        if (j->is_null() && !checked) {
            bool numeric = !token.empty();
            for (size_t n = 0; n < token.size(); ++n)
                numeric = numeric && token[n] >= '0' && token[n] <= '9';
            *j = (unflatten ? token == "0" : numeric || token == "-") ? array() : object();
        }
        if (j->is_object())
            j = checked ? &j->at(token) : &(*j)[token];
        else if (j->is_array()) {
            if (token == "-" && !unflatten) {
                if (checked)
                    throw out_of_range(402, "array index '-' (" + detail::index_string(j->size()) +
                                                ") is out of range");
                j = &(*j)[j->size()];
            } else {
                size_t n = ArrayIndex(token);
                j = checked ? &j->at(n) : &(*j)[n];
            }
        } else {
            if (unflatten)
                throw type_error(313, "invalid value to unflatten");
            throw out_of_range(404, "unresolved reference token '" + token + "'");
        }
    }
    return *j;
}
json &json::operator[](const json_pointer &p) { return pointer_get(p, false, false); }
const json &json::operator[](const json_pointer &p) const {
    return const_cast<json *>(this)->pointer_get(p, true, false);
}
json &json::at(const json_pointer &p) { return pointer_get(p, true, false); }
const json &json::at(const json_pointer &p) const {
    return const_cast<json *>(this)->pointer_get(p, true, false);
}
void json::flatten_into(const std::string &path, json &out) const {
    if (is_object() && !empty()) {
        for (object_t::const_iterator i = object_->begin(); i != object_->end(); ++i)
            i->second.flatten_into(path + "/" + EscapePointer(i->first), out);
    } else if (is_array() && !empty()) {
        for (size_t i = 0; i < size(); ++i)
            (*array_)[i].flatten_into(path + "/" + detail::index_string(i), out);
    } else
        out[path] = (is_object() || is_array()) ? json() : *this;
}
json json::flatten() const {
    json out = object();
    flatten_into("", out);
    return out;
}
json json::unflatten() const {
    if (!is_object())
        throw type_error(314, "only objects can be unflattened");
    json out;
    for (object_t::const_iterator i = object_->begin(); i != object_->end(); ++i) {
        if (!i->second.is_primitive())
            throw type_error(315, "values in object must be primitive");
        out.pointer_get(json_pointer(i->first), false, true) = i->second;
    }
    return out;
}

static void Hex4(std::string &out, unsigned cp) {
    const char *h = "0123456789abcdef";
    out += "\\u";
    out += h[(cp >> 12) & 15];
    out += h[(cp >> 8) & 15];
    out += h[(cp >> 4) & 15];
    out += h[cp & 15];
}
static std::string HexByte(unsigned byte) {
    const char *h = "0123456789ABCDEF";
    std::string r;
    r += h[(byte >> 4) & 15];
    r += h[byte & 15];
    return r;
}
static void Quote(std::string &out, const std::string &text, bool ascii) {
    out += '"';
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        switch (c) {
        case '"':
            out += "\\\"";
            continue;
        case '\\':
            out += "\\\\";
            continue;
        case '\b':
            out += "\\b";
            continue;
        case '\t':
            out += "\\t";
            continue;
        case '\n':
            out += "\\n";
            continue;
        case '\f':
            out += "\\f";
            continue;
        case '\r':
            out += "\\r";
            continue;
        default:
            break;
        }
        if (c < 0x20) {
            Hex4(out, c);
            continue;
        }
        if (c < 0x80) {
            out += c;
            continue;
        }
        size_t start = i;
        unsigned count = c >= 0xC2 && c <= 0xDF   ? 1
                         : c >= 0xE0 && c <= 0xEF ? 2
                         : c >= 0xF0 && c <= 0xF4 ? 3
                                                  : 0;
        if (!count)
            throw json::type_error(316, "invalid UTF-8 byte at index " + detail::index_string(i) + ": 0x" +
                                            HexByte(c));
        unsigned cp = c & (0x7F >> count);
        for (unsigned k = 0; k < count; ++k) {
            if (++i == text.size())
                throw json::type_error(316, "incomplete UTF-8 string; last byte: 0x" +
                                                HexByte(static_cast<unsigned char>(text[i - 1])));
            unsigned char next = static_cast<unsigned char>(text[i]);
            bool valid = (next & 0xC0) == 0x80;
            if (k == 0) {
                if (c == 0xE0)
                    valid = valid && next >= 0xA0;
                if (c == 0xED)
                    valid = valid && next <= 0x9F;
                if (c == 0xF0)
                    valid = valid && next >= 0x90;
                if (c == 0xF4)
                    valid = valid && next <= 0x8F;
            }
            if (!valid)
                throw json::type_error(316, "invalid UTF-8 byte at index " + detail::index_string(i) +
                                                ": 0x" + HexByte(next));
            cp = (cp << 6) | (next & 63);
        }
        if (ascii) {
            if (cp <= 0xFFFF)
                Hex4(out, cp);
            else {
                cp -= 0x10000;
                Hex4(out, 0xD800 + (cp >> 10));
                Hex4(out, 0xDC00 + (cp & 1023));
            }
        } else
            out.append(text, start, i - start + 1);
    }
    out += '"';
}
void json::append(std::string &out, int indent, char indent_char, bool ascii, unsigned depth) const {
    char number[64];
    switch (type_) {
    case detail::value_t::null:
    case detail::value_t::discarded:
        out += is_discarded() ? "<discarded>" : "null";
        break;
    case detail::value_t::boolean:
        out += signed_ ? "true" : "false";
        break;
    case detail::value_t::number_integer:
        _snprintf_s(number, sizeof(number), _TRUNCATE, "%I64d", signed_);
        out += number;
        break;
    case detail::value_t::number_unsigned:
        _snprintf_s(number, sizeof(number), _TRUNCATE, "%I64u", unsigned_);
        out += number;
        break;
    case detail::value_t::number_float:
        if (!ShipyardJsonAlgorithm::finite(real_))
            out += "null";
        else {
            char *end = ShipyardJsonAlgorithm::detail::to_chars(number, number + 64, real_);
            out.append(number, end);
        }
        break;
    case detail::value_t::string:
        Quote(out, text_, ascii);
        break;
    case detail::value_t::object:
    case detail::value_t::array: {
        bool obj = is_object();
        out += obj ? '{' : '[';
        object_t::const_iterator it;
        if (obj)
            it = object_->begin();
        for (size_t i = 0; i < size(); ++i) {
            if (i)
                out += ',';
            if (indent >= 0) {
                out += '\n';
                out.append(static_cast<size_t>(depth + 1) * indent, indent_char);
            }
            if (obj) {
                Quote(out, it->first, ascii);
                out += indent >= 0 ? ": " : ":";
                it->second.append(out, indent, indent_char, ascii, depth + 1);
                ++it;
            } else
                (*array_)[i].append(out, indent, indent_char, ascii, depth + 1);
        }
        if (!empty() && indent >= 0) {
            out += '\n';
            out.append(static_cast<size_t>(depth) * indent, indent_char);
        }
        out += obj ? '}' : ']';
        break;
    }
    default:
        throw type_error(302, "unsupported JSON value type");
    }
}
std::string json::dump(int indent, char indent_char, bool ascii) const {
    std::string result;
    append(result, indent, indent_char, ascii, 0);
    return result;
}
#include "json/generated/stream_writer.hpp"
std::ostream &operator<<(std::ostream &stream, const json &value) {
    const bool pretty = stream.width() > 0;
    const std::streamsize indentation = pretty ? stream.width() : 0;
    stream.width(0);
    StreamWriter writer(stream, stream.fill());
    writer.dump(value, pretty, false, static_cast<unsigned int>(indentation));
    return stream;
}
bool json::operator==(const json &r) const {
    if (type_ == r.type_) {
        switch (type_) {
        case detail::value_t::null:
            return true;
        case detail::value_t::object:
            return *object_ == *r.object_;
        case detail::value_t::array:
            return *array_ == *r.array_;
        case detail::value_t::string:
            return text_ == r.text_;
        case detail::value_t::boolean:
        case detail::value_t::number_integer:
            return signed_ == r.signed_;
        case detail::value_t::number_unsigned:
            return unsigned_ == r.unsigned_;
        case detail::value_t::number_float:
            return real_ == r.real_;
        default:
            return false;
        }
    }
    if (is_number_integer() && r.is_number_integer())
        return is_number_unsigned() ? unsigned_ == static_cast<unsigned long long>(r.signed_)
                                    : static_cast<unsigned long long>(signed_) == r.unsigned_;
    if ((is_number_float() || is_number_integer()) && (r.is_number_float() || r.is_number_integer()))
        return get<double>() == r.get<double>();
    return false;
}
bool json::operator!=(const json& r) const {
    // Pinned 3.12.0 treats discarded and NaN/number comparisons as unordered.
    if (is_discarded() || r.is_discarded()) return false;
    if ((is_number_float() && _isnan(real_) && (r.is_number_integer() || r.is_number_float())) ||
        (r.is_number_float() && _isnan(r.real_) && (is_number_integer() || is_number_float()))) return false;
    return !(*this == r);
}
} // namespace nlohmann

#include "json/generated/ordered_json.cpp"
