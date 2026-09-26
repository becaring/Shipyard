"""Version-constrained Prism syntax lowering for the two audited Xbox ports.

The caller owns the C++ parser dependency, source locking, resource acquisition,
shader builder, and diagnostic policy. Only compiler compatibility is shared.
"""
import json
import re

SUPPORTED_REVISIONS = frozenset((
    'bbcbc7e3f890a5806b579361e7aa0336acd547e7',
    '1de054450e7b3c5f777d2e3dfcb228ad120c329d',
))


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('Expected exactly one Prism anchor: ' + old[:100])
    return text.replace(old, new, 1)

def walk(node):
    yield node
    for child in node.named_children:
        yield from walk(child)


def tx(node):
    return node.text.decode()


def field(node, name):
    return node.child_by_field_name(name)


def _apply_edits(source, edits):
    # Tree-sitter offsets refer to the original bytes. Replace right-to-left.
    for start, end, replacement in sorted(edits, reverse=True):
        source = source[:start] + replacement + source[end:]
    return source


def _range_replacement(source, node, begin='PrismBegin', end_func='PrismEnd'):
    right = field(node, 'right')
    body = field(node, 'body')
    declarator = field(node, 'declarator')
    if right.type == 'initializer_list':
        raise ValueError('Unaudited initializer-list range')

    opening = next(child for child in node.children if child.type == '(')
    declaration = source[opening.end_byte:declarator.end_byte].decode().strip()
    suffix = str(node.start_byte)
    range_name = '_prismRange' + suffix
    iterator = '_prismIt' + suffix
    end_name = '_prismEnd' + suffix
    inner = tx(body)[1:-1] if body.type == 'compound_statement' else tx(body)
    return (
        '{ auto&& ' + range_name + ' = ' + tx(right) + '; auto ' + iterator +
        ' = ' + begin + '(' + range_name + '); auto ' + end_name + ' = ' + end_func + '(' +
        range_name + '); for (; ' + iterator + ' != ' + end_name + '; ++' +
        iterator + ') { ' + declaration + ' = *' + iterator + '; ' + inner +
        ' } }'
    ).encode()


def lower_ranges(source, parser, begin='PrismBegin', end='PrismEnd'):
    # Lower innermost loops first. The end expression is cached once, as
    # C++ range-for requires, and each parse supplies fresh byte offsets.
    for _iteration in range(64):
        root = parser.parse(source).root_node
        edits = []
        for node in walk(root):
            if node.type != 'for_range_loop':
                continue
            if any(child.type == 'for_range_loop' for child in list(walk(node))[1:]):
                continue
            edits.append((node.start_byte, node.end_byte,
                          _range_replacement(source, node, begin, end)))
        if not edits:
            return source
        source = _apply_edits(source, edits)
    raise ValueError('Unexpected range nesting')


def aggregates(text, parser):
    """Add value-preserving constructors only to pure data AST node structs."""
    data = text.encode()
    edits = []
    for node in walk(parser.parse(data).root_node):
        if node.type != 'struct_specifier':
            continue
        name = field(node, 'name')
        body = field(node, 'body')
        if not name or not body:
            continue
        members = [child for child in body.named_children
                   if child.type != 'comment']
        if not members or any(child.type != 'field_declaration'
                              for child in members):
            continue

        fields = []
        for member in members:
            type_node = field(member, 'type')
            declarators = [child for child in member.named_children
                           if child != type_node and child.type != 'comment']
            if any(child.type != 'field_identifier' for child in declarators):
                fields = []
                break
            fields.extend((tx(type_node), tx(child)) for child in declarators)
        if not fields:
            continue

        parameters = ', '.join('const ' + typ + '& ' + field_name + '_ = ' +
                               typ + '()' for typ, field_name in fields)
        initializers = ', '.join(field_name + '(' + field_name + '_)'
                                 for _, field_name in fields)
        constructor = ('\n    explicit ' + tx(name) + '(' + parameters +
                       ') : ' + initializers + ' {}\n')
        edits.append((body.start_byte + 1, body.start_byte + 1,
                      constructor.encode()))
    return _apply_edits(data, edits).decode()


def braces(text, parser):
    data = text.encode()
    edits = []
    for node in walk(parser.parse(data).root_node):
        if node.type == 'compound_literal_expression':
            value = field(node, 'value')
        elif node.type == 'new_expression':
            value = next((child for child in node.named_children
                          if child.type == 'initializer_list'), None)
        else:
            continue
        if value and value.type == 'initializer_list':
            edits.extend(((value.start_byte, value.start_byte + 1, b'('),
                          (value.end_byte - 1, value.end_byte, b')')))
    return _apply_edits(data, set(edits)).decode()


def enum_wrappers(text):
    def lower(match):
        name, body = match[1], match[2]
        return ('struct ' + name + ' { enum Value {' + body +
                '}; Value value; ' + name +
                '(Value v) : value(v) {} operator Value() const { return value; } };')
    return re.sub(r'enum class (\w+)\s*\{([^}]+)\};', lower, text)


def _lower_common_syntax(text):
    text = text.replace('#include <variant>', '#include <xbox_compat/variant.h>')
    text = text.replace('#include <optional>', '#include <xbox_compat/optional.h>')
    for word in ('variant', 'optional', 'get', 'holds_alternative', 'nullopt'):
        text = re.sub(r'\bstd::' + word + r'\b', 'xbox_compat::' + word, text)
    text = text.replace('[[nodiscard]] ', '')
    text = text.replace(' noexcept override', ' throw()')
    text = text.replace(' noexcept', ' throw()')
    text = enum_wrappers(text)
    for namespace in ('lexer', 'ast', 'gv'):
        text = text.replace('namespace prism::' + namespace + ' {',
                            'namespace prism { namespace ' + namespace + ' {')
        text = text.replace('} // namespace prism::' + namespace,
                            '} } // namespace prism::' + namespace)
    return text


def _lower_processor_header(text):
    if text.count('#define M_ARRAY') != 1 or text.count('#define CONTAINS') != 1:
        raise ValueError('Expected Prism processor macro anchors')
    start = text.index('#define M_ARRAY')
    end = text.index('#define CONTAINS', start)
    macros = ('#define M_ARRAY(arr,type,...) prism::MTDArray<type>'
              '((uintptr_t)&arr,PrismDimensions(__VA_ARGS__))\n'
              '#define VAR(name,type) std::make_pair(name,prism::ContextTypes(type))\n\n')
    text = text[:start] + macros + text[end:]
    replacements = (
        ('uintptr_t ptr = 0;',
         'uintptr_t ptr;\n    MTDArray() : ptr(0) {}\n'
         '    MTDArray(uintptr_t p,const std::vector<size_t>& d) : ptr(p),dimensions(d) {}'),
        ('int depth = 0;', 'int depth;'),
        ('parent(std::move(parent))', 'parent(std::move(parent)), depth(0)'),
        ('ScopeType scope = ScopeType::None;', 'ScopeType scope;'),
        ('bool skipUntilEnd = false;',
         'bool skipUntilEnd;\n'
         '    RuntimeContext() : scope(ScopeType::None),skipUntilEnd(false) {}'),
        ('IncludeFunc m_include_loader = nullptr;', 'IncludeFunc m_include_loader;'),
        ('class Processor {\n  public:',
         'class Processor {\n  public:\n    Processor() : m_include_loader(nullptr) {}'),
    )
    for old, new in replacements:
        text = once(text, old, new)
    return text


def _lower_file_contract(text, name):
    if name == 'lexer.h':
        text = once(text, 'size_t pos = 0;', 'size_t pos;')
        text = once(text, ': input(text)', ': input(text), pos(0)')
    if name == 'lexer.cpp':
        text = re.sub(r'tokens\.emplace_back\((.*)\);',
                      r'tokens.push_back(Token(\1));', text)
    if name == 'ast.h':
        text = once(text, 'size_t pos = 0;', 'size_t pos;')
        text = once(text, ': tokens(std::move(tokenList))',
                    ': tokens(std::move(tokenList)), pos(0)')
    if name == 'processor.h':
        text = _lower_processor_header(text)
    if name == 'processor.cpp':
        if text.count('return { start, c };') != 2:
            raise ValueError('Unexpected iterator string returns')
        text = text.replace('return { start, c };',
                            'return std::string(start, c);')
    if name == 'gv.cpp':
        text = once(
            text,
            'std::sregex_token_iterator first{ line.begin(), line.end(), re, -1 }, last;',
            'std::sregex_token_iterator first(line.begin(), line.end(), re, -1), last;',
        )
    return text


def lower(text, name, parser):
    text = _lower_common_syntax(text)
    text = _lower_file_contract(text, name)
    text = re.sub(r'R"\((.*?)\)"', lambda match: json.dumps(match[1]),
                  text, flags=re.S)
    if name.endswith('.h'):
        text = aggregates(text, parser)
    text = braces(text, parser)
    text = lower_ranges(text.encode(), parser).decode()
    if name != 'invoke.h' and (name.endswith('.h') or name.endswith('.cpp')):
        text = '#include <prism/compat.h>\n' + text
    return text


class PrismProjector:
    """Lower reviewed Prism syntax without acquiring parser or source inputs."""

    def __init__(self, parser, revision):
        if revision not in SUPPORTED_REVISIONS:
            raise ValueError('Unsupported Prism revision: ' + str(revision))
        self.parser = parser
        self.revision = revision

    def lower(self, text, name):
        return lower(text, name, self.parser)

    def lower_ranges(self, source):
        return lower_ranges(source, self.parser)

    def aggregates(self, text):
        return aggregates(text, self.parser)
