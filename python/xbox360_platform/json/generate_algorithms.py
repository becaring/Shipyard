"""Generated pinned JSON lexical/numeric algorithms; no semantic substitution."""
from pathlib import Path
import hashlib
import re

# Locked below after validating the official commit in prepare_json_xdk.py.
HASHES = {'lexer': 'a8b73e55242731375c088133f3a355af7b1bb82d6b9531678cb20560d3733c1d', 'to_chars': '20b9bc4469ee490bc626d8dbcdee060a494292473df4bdbcecfe2f2145ed562d'}

def exact(text, before, after, count=1):
    if text.count(before) != count:
        raise ValueError('algorithm anchor changed: '+repr(before))
    return text.replace(before, after)

def generate(source, out):
    results={}
    outputs=[]
    for name,relative in [('lexer','detail/input/lexer.hpp'),('to_chars','detail/conversions/to_chars.hpp')]:
        if hashlib.sha256((source/'include/nlohmann'/relative).read_bytes()).hexdigest()!=HASHES[name]:
            raise ValueError(name+' source mismatch')
    for name, relative in [('lexer','detail/input/lexer.hpp'),('to_chars','detail/conversions/to_chars.hpp')]:
        path=source/'include/nlohmann'/relative
        raw=path.read_bytes()
        if hashlib.sha256(raw).hexdigest()!=HASHES[name]: raise ValueError(name+' source mismatch')
        s=raw.decode().replace('\r\n','\n')
        # Only identifier tokens outside comments/strings are rewritten; each whole source is SHA-locked.
        def token(m):
            t=m.group()
            return '' if t=='noexcept' else t
        s=re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\bnoexcept\b',token,s)
        s=re.sub(r'#include <nlohmann/[^>]+>', '#include "json/algorithm_support.h"',s)
        s=s.replace('#include <initializer_list> // initializer_list','// Fixed arity byte range carrier, preserving order and storage lifetime.')
        if name=='lexer':
            s=exact(s,'enum class token_type','struct token_scope { enum type')
            s=exact(s,'literal_or_value  ///< a literal or the begin of a value (only for diagnostics)\n    };','literal_or_value  ///< a literal or the begin of a value (only for diagnostics)\n    }; }; typedef typename token_scope::type token_type;')
            s=s.replace('token_type::','token_scope::')
            s=re.sub(r'    using (\w+) = ([^;]+);',r'    typedef \2 \1;',s)
            s=exact(s,'    typedef typename lexer_base<BasicJsonType>::token_type token_type;', '    typedef typename lexer_base<BasicJsonType>::token_type token_type; typedef typename lexer_base<BasicJsonType>::token_scope token_scope;')
            s=exact(s,', decimal_point_char(static_cast<char_int_type>(get_decimal_point()))',', current(char_traits<char_type>::eof()), next_unget(false), error_message(""), value_integer(0), value_unsigned(0), value_float(0), decimal_point_char(static_cast<char_int_type>(get_decimal_point())), decimal_point_position(std::string::npos)')
            s=exact(s,'    lexer(const lexer&) = delete;','  private: lexer(const lexer&); public:')
            s=exact(s,'    lexer& operator=(lexer&) = delete;','  private: lexer& operator=(lexer&); public:')
            s=re.sub(r'    lexer(?:& operator=)?\(lexer&&\) = default;[^\n]*','    // Move is not reached; the concrete parser owns its lexer directly.',s)
            s=exact(s,'    ~lexer() = default;','    ~lexer() {}')
            s=exact(s,'        const auto factors = { 12u, 8u, 4u, 0u };\n        for (const auto factor : factors)','        const unsigned factors[] = { 12u, 8u, 4u, 0u };\n        for (size_t factor_i=0;factor_i<4;++factor_i)')
            s=exact(s,'            get();\n\n            if (current >= \'0\'', '            const unsigned factor=factors[factor_i]; get();\n\n            if (current >= \'0\'')
            s=exact(s,'std::initializer_list<char_int_type> ranges','ByteRanges ranges')
            s=re.sub(r'next_byte_in_range\(\{([^}]+)\}\)',r'next_byte_in_range(ByteRanges(\1))',s)
            s=exact(s,'        for (const auto c : token_string)\n        {','        for (size_t token_i=0;token_i<token_string.size();++token_i)\n        { const char_type c=token_string[token_i];')
            s=exact(s,'                std::array<char, 9> cs{{}};','                std::array<char, 9> cs = {{0}};')
            s=s.replace('(std::snprintf)','(_snprintf)')
            s=exact(s,'case char_traits<char_type>::eof():', 'case EOF:', 4)
            s=s.replace('std::strtoull','_strtoui64').replace('std::strtoll','_strtoi64')
            s=s.replace('std::strtof(str, endptr)','static_cast<float>(std::strtod(str, endptr))').replace('std::strtold(str, endptr)','static_cast<long double>(std::strtod(str, endptr))')
            s=re.sub(r'\bconstexpr\s+', '',s)
            for before,after in [('const bool ignore_comments = false','const bool ignore_comments'),('char_int_type current = char_traits<char_type>::eof()','char_int_type current'),('bool next_unget = false','bool next_unget'),('position_t position {}','position_t position'),('std::vector<char_type> token_string {}','std::vector<char_type> token_string'),('string_t token_buffer {}','string_t token_buffer'),('const char* error_message = ""','const char* error_message'),('number_integer_t value_integer = 0','number_integer_t value_integer'),('number_unsigned_t value_unsigned = 0','number_unsigned_t value_unsigned'),('number_float_t value_float = 0','number_float_t value_float'),("const char_int_type decimal_point_char = '.'",'const char_int_type decimal_point_char'),('std::size_t decimal_point_position = std::string::npos','std::size_t decimal_point_position')]: s=exact(s,before,after)
        else:
            s=exact(s,'    constexpr diyfp(', '    diyfp(')
            s=re.sub(r'\bconstexpr\s+', 'const ',s)
            s=exact(s,'    std::uint64_t f = 0;','    std::uint64_t f;')
            s=exact(s,'    int e = 0;','    int e;')
            s=exact(s,'std::uint32_t pow10{};', 'std::uint32_t pow10 = 0;')
            s=exact(s,'return {x.f - y.f, x.e};','return diyfp(x.f - y.f, x.e);')
            s=exact(s,'return {h, x.e + y.e + 64};','return diyfp(h, x.e + y.e + 64);')
            s=exact(s,'return {x.f << delta, target_exponent};','return diyfp(x.f << delta, target_exponent);')
            s=exact(s,'    diyfp plus;','    diyfp plus; boundaries(diyfp a,diyfp b,diyfp c):w(a),minus(b),plus(c) {}')
            s=exact(s,'return {diyfp::normalize(v), w_minus, w_plus};','return boundaries(diyfp::normalize(v), w_minus, w_plus);')
            s=re.sub(r'std::uint64_t\{(\w+)\}',r'std::uint64_t(\1)',s)
            s=exact(s,'using bits_type = typename std::conditional<kPrecision == 24, std::uint32_t, std::uint64_t >::type;','typedef typename std::conditional<kPrecision == 24, std::uint32_t, std::uint64_t >::type bits_type;')
            s=s.replace('std::isfinite','ShipyardJsonAlgorithm::finite').replace('std::signbit','ShipyardJsonAlgorithm::signbit')
            s=s.replace('std::numeric_limits<FloatType>::max_digits10','(2 + std::numeric_limits<FloatType>::digits * 30103 / 100000)')
        guard='SHIPYARD_JSON_GENERATED_'+name.upper()+'_H'
        generated=('#ifndef '+guard+'\n#define '+guard+'\n#line 1 "'+path.as_posix()+'"\n'+s+'\n#endif\n').encode()
        target=out/(name+'.hpp'); outputs.append((target,generated))
        results[relative]={'source_sha256':HASHES[name],'output_sha256':hashlib.sha256(generated).hexdigest()}
    for target,generated in outputs:
        target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(generated)
    return results
