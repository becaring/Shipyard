"""Project the pinned serializer's text/UTF-8 routines onto the bounded DOM.

Binary DOM storage is absent, so binary serialization remains an explicit gap.
Stream output retains upstream put/write boundaries and UTF-8 buffering.
"""

from pathlib import Path
import hashlib


HASH = "4c079acb8f6936c4f787fd5706a52c77a1751dd34787a821705845698ec8540e"


def method(s, signature):
    start = s.index(signature)
    opening = s.index("\n    {", start) + 5
    # All extracted functions have a closing brace at indentation four.
    end = s.index("\n    }", opening) + 6
    return s[start:end]


def generate(source, out):
    serializer = source / "include/nlohmann/detail/output/serializer.hpp"
    raw = serializer.read_bytes()
    if hashlib.sha256(raw).hexdigest() != HASH:
        raise ValueError("serializer source mismatch")
    source_text = raw.decode().replace("\r\n", "\n")

    dump = method(source_text, "    void dump(const BasicJsonType& val,")
    binary_start = dump.index("            case value_t::binary:")
    boolean_start = dump.index("            case value_t::boolean:", binary_start)
    dump = dump[:binary_start] + dump[boolean_start:]
    dump = dump.replace("BasicJsonType", "json")
    dump = dump.replace("val.m_data.m_type", "val.type_")
    dump = dump.replace("value_t::", "detail::value_t::")
    value_fields = [
        ("object", "object_"),
        ("array", "array_"),
        ("number_integer", "signed_"),
        ("number_unsigned", "unsigned_"),
        ("number_float", "real_"),
        ("boolean", "signed_"),
    ]
    for upstream, projected in value_fields:
        dump = dump.replace("val.m_data.m_value." + upstream, "val." + projected)
    dump = dump.replace("*val.m_data.m_value.string", "val.text_")

    escaped = method(source_text, "    void dump_escaped(const string_t& s, const bool ensure_ascii)")
    escaped = escaped.replace("std::uint32_t codepoint{};", "std::uint32_t codepoint = 0;")
    escaped = escaped.replace("(std::snprintf)", "(_snprintf)")
    escaped = escaped.replace("detail::binary_writer<BasicJsonType, char>::to_char_type", "static_cast<char>")
    escaped = escaped.replace(
        'JSON_THROW(type_error::create(316, concat("invalid UTF-8 byte at index ", std::to_string(i), ": 0x", hex_bytes(byte | 0)), nullptr));',
        'throw json::type_error(316, std::string("invalid UTF-8 byte at index ") + detail::index_string(i) + ": 0x" + hex_bytes(byte | 0));',
    )
    escaped = escaped.replace(
        'JSON_THROW(type_error::create(316, concat("incomplete UTF-8 string; last byte: 0x", hex_bytes(static_cast<std::uint8_t>(s.back() | 0))), nullptr));',
        'throw json::type_error(316, std::string("incomplete UTF-8 string; last byte: 0x") + hex_bytes(static_cast<std::uint8_t>(s.back() | 0)));',
    )
    helper = method(source_text, "    static std::string hex_bytes(").replace("constexpr ", "")
    decode = method(source_text, "    static std::uint8_t decode(").replace(" noexcept", "")
    generated = (
        """// Generated from pinned nlohmann 3.12.0 serializer.hpp.
// SPDX-FileCopyrightText: 2008 - 2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>
// SPDX-FileCopyrightText: 2013 - 2025 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT
// Include inside namespace nlohmann after detail::index_string is declared.
class StreamWriter {
    typedef std::string string_t;
    static const std::uint8_t UTF8_ACCEPT = 0, UTF8_REJECT = 1;
    struct error_handler_t { enum type { strict, replace, ignore }; };
    struct Output {
        std::ostream &stream;
        explicit Output(std::ostream &s):stream(s){}
        void write_character(char c) { stream.put(c); }
        void write_characters(const char *s, std::size_t n) { stream.write(s, static_cast<std::streamsize>(n)); }
    } output;
    Output *o;
    std::array<char,512> string_buffer;
    const char indent_char;
    string_t indent_string;
    const error_handler_t::type error_handler;
    template<class T> void dump_integer(T x) {
        if (x == 0) { o->write_character('0'); return; }
        const std::string text = json(x).dump();
        o->write_characters(text.data(), text.size());
    }
    void dump_float(double x) {
        const std::string text = json(x).dump();
        o->write_characters(text.data(), text.size());
    }
public:
    StreamWriter(std::ostream &s, char fill) : output(s), o(&output), indent_char(fill), indent_string(512, fill), error_handler(error_handler_t::strict) {}
"""
        + dump
        + "\nprivate:\n"
        + escaped
        + "\n"
        + helper
        + "\n"
        + decode
        + "\n};\n"
    )
    destination = out / "stream_writer.hpp"
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(generated)
    return {
        "detail/output/serializer.hpp": {
            "source_sha256": HASH,
            "output_sha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
            "projection": (
                "text DOM dump, dump_escaped, hex_bytes, decode; native binary storage unavailable; "
                "scalar formatting uses existing pinned Grisu2/decimal backend"
            ),
        }
    }
