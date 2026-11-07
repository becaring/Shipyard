"""Adapt the pinned JSON lexer and number conversion algorithms for XDK."""

import hashlib
from pathlib import Path
import re


# Raw upstream bytes at the pinned nlohmann commit.
HASHES = {
    "lexer": "a8b73e55242731375c088133f3a355af7b1bb82d6b9531678cb20560d3733c1d",
    "to_chars": "20b9bc4469ee490bc626d8dbcdee060a494292473df4bdbcecfe2f2145ed562d",
}
INPUTS = (
    ("lexer", "detail/input/lexer.hpp"),
    ("to_chars", "detail/conversions/to_chars.hpp"),
)


def exact(text, before, after, count=1):
    """Replace a required upstream anchor, rejecting a changed input."""
    if text.count(before) != count:
        raise ValueError("algorithm anchor changed: " + repr(before))
    return text.replace(before, after)


def _without_noexcept(match):
    # The regex alternation leaves comments and quoted strings unchanged.
    token = match.group()
    return "" if token == "noexcept" else token


def _project_common(text):
    text = re.sub(
        r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\bnoexcept\b',
        _without_noexcept,
        text,
    )
    text = re.sub(r"#include <nlohmann/[^>]+>", '#include "json/algorithm_support.h"', text)
    return text.replace(
        "#include <initializer_list> // initializer_list",
        "// Fixed arity byte range carrier, preserving order and storage lifetime.",
    )


def _project_lexer(text):
    text = exact(text, "enum class token_type", "struct token_scope { enum type")
    text = exact(
        text,
        "literal_or_value  ///< a literal or the begin of a value (only for diagnostics)\n    };",
        "literal_or_value  ///< a literal or the begin of a value (only for diagnostics)\n"
        "    }; }; typedef typename token_scope::type token_type;",
    )
    text = text.replace("token_type::", "token_scope::")
    text = re.sub(r"    using (\w+) = ([^;]+);", r"    typedef \2 \1;", text)
    text = exact(
        text,
        "    typedef typename lexer_base<BasicJsonType>::token_type token_type;",
        "    typedef typename lexer_base<BasicJsonType>::token_type token_type;"
        " typedef typename lexer_base<BasicJsonType>::token_scope token_scope;",
    )
    text = exact(
        text,
        ", decimal_point_char(static_cast<char_int_type>(get_decimal_point()))",
        ', current(char_traits<char_type>::eof()), next_unget(false), error_message("")'
        ", value_integer(0), value_unsigned(0), value_float(0),"
        " decimal_point_char(static_cast<char_int_type>(get_decimal_point())),"
        " decimal_point_position(std::string::npos)",
    )
    text = exact(text, "    lexer(const lexer&) = delete;", "  private: lexer(const lexer&); public:")
    text = exact(text, "    lexer& operator=(lexer&) = delete;", "  private: lexer& operator=(lexer&); public:")
    text = re.sub(
        r"    lexer(?:& operator=)?\(lexer&&\) = default;[^\n]*",
        "    // Move is not reached; the concrete parser owns its lexer directly.",
        text,
    )
    text = exact(text, "    ~lexer() = default;", "    ~lexer() {}")
    text = exact(
        text,
        "        const auto factors = { 12u, 8u, 4u, 0u };\n        for (const auto factor : factors)",
        "        const unsigned factors[] = { 12u, 8u, 4u, 0u };\n"
        "        for (size_t factor_i=0;factor_i<4;++factor_i)",
    )
    text = exact(
        text,
        "            get();\n\n            if (current >= '0'",
        "            const unsigned factor=factors[factor_i]; get();\n\n            if (current >= '0'",
    )
    text = exact(text, "std::initializer_list<char_int_type> ranges", "ByteRanges ranges")
    text = re.sub(r"next_byte_in_range\(\{([^}]+)\}\)", r"next_byte_in_range(ByteRanges(\1))", text)
    text = exact(
        text,
        "        for (const auto c : token_string)\n        {",
        "        for (size_t token_i=0;token_i<token_string.size();++token_i)\n"
        "        { const char_type c=token_string[token_i];",
    )
    text = exact(text, "                std::array<char, 9> cs{{}};", "                std::array<char, 9> cs = {{0}};")
    text = text.replace("(std::snprintf)", "(_snprintf)")
    text = exact(text, "case char_traits<char_type>::eof():", "case EOF:", 4)
    text = text.replace("std::strtoull", "_strtoui64").replace("std::strtoll", "_strtoi64")
    text = text.replace("std::strtof(str, endptr)", "static_cast<float>(std::strtod(str, endptr))")
    text = text.replace("std::strtold(str, endptr)", "static_cast<long double>(std::strtod(str, endptr))")
    text = re.sub(r"\bconstexpr\s+", "", text)

    # These initial values move to the projected constructor.
    member_initializers = (
        ("const bool ignore_comments = false", "const bool ignore_comments"),
        ("char_int_type current = char_traits<char_type>::eof()", "char_int_type current"),
        ("bool next_unget = false", "bool next_unget"),
        ("position_t position {}", "position_t position"),
        ("std::vector<char_type> token_string {}", "std::vector<char_type> token_string"),
        ("string_t token_buffer {}", "string_t token_buffer"),
        ('const char* error_message = ""', "const char* error_message"),
        ("number_integer_t value_integer = 0", "number_integer_t value_integer"),
        ("number_unsigned_t value_unsigned = 0", "number_unsigned_t value_unsigned"),
        ("number_float_t value_float = 0", "number_float_t value_float"),
        ("const char_int_type decimal_point_char = '.'", "const char_int_type decimal_point_char"),
        ("std::size_t decimal_point_position = std::string::npos", "std::size_t decimal_point_position"),
    )
    for before, after in member_initializers:
        text = exact(text, before, after)
    return text


def _project_to_chars(text):
    text = exact(text, "    constexpr diyfp(", "    diyfp(")
    text = re.sub(r"\bconstexpr\s+", "const ", text)
    text = exact(text, "    std::uint64_t f = 0;", "    std::uint64_t f;")
    text = exact(text, "    int e = 0;", "    int e;")
    text = exact(text, "std::uint32_t pow10{};", "std::uint32_t pow10 = 0;")
    text = exact(text, "return {x.f - y.f, x.e};", "return diyfp(x.f - y.f, x.e);")
    text = exact(text, "return {h, x.e + y.e + 64};", "return diyfp(h, x.e + y.e + 64);")
    text = exact(text, "return {x.f << delta, target_exponent};", "return diyfp(x.f << delta, target_exponent);")
    text = exact(
        text, "    diyfp plus;",
        "    diyfp plus; boundaries(diyfp a,diyfp b,diyfp c):w(a),minus(b),plus(c) {}",
    )
    text = exact(
        text, "return {diyfp::normalize(v), w_minus, w_plus};",
        "return boundaries(diyfp::normalize(v), w_minus, w_plus);",
    )
    text = re.sub(r"std::uint64_t\{(\w+)\}", r"std::uint64_t(\1)", text)
    text = exact(
        text,
        "using bits_type = typename std::conditional<kPrecision == 24, std::uint32_t, std::uint64_t >::type;",
        "typedef typename std::conditional<kPrecision == 24, std::uint32_t, std::uint64_t >::type bits_type;",
    )
    text = text.replace("std::isfinite", "ShipyardJsonAlgorithm::finite")
    text = text.replace("std::signbit", "ShipyardJsonAlgorithm::signbit")
    return text.replace(
        "std::numeric_limits<FloatType>::max_digits10",
        "(2 + std::numeric_limits<FloatType>::digits * 30103 / 100000)",
    )


def _wrapped_header(name, path, projected):
    guard = "SHIPYARD_JSON_GENERATED_" + name.upper() + "_H"
    return (
        "#ifndef " + guard + "\n#define " + guard + '\n#line 1 "' + path.as_posix() + '"\n'
        + projected + "\n#endif\n"
    ).encode()


def generate(source, out):
    source = Path(source)
    out = Path(out)
    outputs = []
    results = {}

    # Stage all outputs before writing any; an anchor failure leaves earlier output intact.
    for name, relative in INPUTS:
        path = source / "include/nlohmann" / relative
        raw = path.read_bytes()
        if hashlib.sha256(raw).hexdigest() != HASHES[name]:
            raise ValueError(name + " source mismatch")

        projected = _project_common(raw.decode().replace("\r\n", "\n"))
        if name == "lexer":
            projected = _project_lexer(projected)
        else:
            projected = _project_to_chars(projected)

        generated = _wrapped_header(name, Path("nlohmann") / relative, projected)
        outputs.append((out / (name + ".hpp"), generated))
        results[relative] = {
            "source_sha256": HASHES[name],
            "output_sha256": hashlib.sha256(generated).hexdigest(),
        }

    for target, generated in outputs:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(generated)
    return results
