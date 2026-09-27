"""Compiler-compatibility lowering shared by active Xbox consumers."""

from dataclasses import dataclass


@dataclass(frozen=True)
class MemberDefault:
    type: str
    name: str
    value: str
    is_array: bool


def collect_member_defaults(text, classname, parser, walk, parse_source=None):
    source = text.encode()
    if parse_source is not None and len(parse_source) != len(source):
        raise ValueError('Masked C++ source must preserve byte offsets')
    classes = [node for node in walk(parser.parse(parse_source or source).root_node)
               if node.type in ('class_specifier', 'struct_specifier')
               and node.child_by_field_name('name')
               and node.child_by_field_name('body')
               and node.child_by_field_name('name').text.decode() == classname]
    if len(classes) != 1:
        raise ValueError('Expected single class ' + classname)
    edits = []
    fields = []
    for member in classes[0].child_by_field_name('body').named_children:
        if member.type != 'field_declaration':
            continue
        if any(node.type == 'storage_class_specifier' and node.text == b'static'
               for node in member.named_children):
            continue
        typ = member.child_by_field_name('type')
        declarators = member.children_by_field_name('declarator')
        for default in member.children_by_field_name('default_value'):
            declarator = next((node for node in reversed(declarators)
                               if node.end_byte <= default.start_byte), None)
            # tree-sitter can classify an empty inline method followed by a
            # semicolon as a field with a brace initializer. It has no data
            # member default to move into a constructor.
            if (declarator is not None and declarator.type == 'function_declarator' and
                    declarator.child_by_field_name('declarator').type == 'field_identifier' and
                    default.type == 'initializer_list' and not default.named_children):
                continue
            if declarator is None or declarator.type == 'function_declarator' or typ is None:
                raise ValueError('Unsupported member default ' + member.text.decode())
            names = [node for node in walk(declarator) if node.type == 'field_identifier']
            if len(names) != 1:
                raise ValueError('Ambiguous member name')
            is_array = declarator.type == 'array_declarator'
            value = default.text.decode()
            if not is_array and default.type == 'initializer_list':
                value = value[1:-1].strip()
            fields.append(MemberDefault(typ.text.decode(), names[0].text.decode(), value, is_array))
            edits.append((declarator.end_byte, default.end_byte))
    for start, end in sorted(edits, reverse=True):
        source = source[:start] + source[end:]
    return source.decode(), fields


def lower_if_initializers(text, parser, walk, expected=None, max_depth=16):
    data = text.encode()
    converted = 0
    for _ in range(max_depth):
        edits = []
        for node in walk(parser.parse(data).root_node):
            if node.type != 'if_statement':
                continue
            condition = node.child_by_field_name('condition')
            initializer = condition.child_by_field_name('initializer') if condition else None
            if not initializer:
                continue
            if any(
                child.type == 'if_statement'
                and child.child_by_field_name('condition')
                and child.child_by_field_name('condition').child_by_field_name('initializer')
                for child in list(walk(node))[1:]
            ):
                continue
            value = condition.child_by_field_name('value')
            replacement = b'{ ' + initializer.text + b' if (' + value.text + b')' + data[condition.end_byte:node.end_byte] + b' }'
            edits.append((node.start_byte, node.end_byte, replacement))
        if not edits:
            if expected is not None and converted != expected:
                raise ValueError('If-initializer inventory drift: ' + str(converted))
            return data.decode()
        converted += len(edits)
        for start, end, replacement in sorted(edits, reverse=True):
            data = data[:start] + replacement + data[end:]
    raise ValueError('Unexpected nested if-initializer depth')


def lift_captureless_callbacks(text, parser, walk, *, parameters, return_type, prefix, expected):
    """Return checked named callback definitions for a consumer-selected signature."""
    data = text.encode()
    selected = []
    for node in walk(parser.parse(data).root_node):
        if node.type != 'lambda_expression':
            continue
        declarator = node.child_by_field_name('declarator')
        if declarator is None or declarator.text.decode() != parameters:
            continue
        capture = next(child for child in node.named_children if child.type == 'lambda_capture_specifier')
        body = node.child_by_field_name('body')
        if capture.text != b'[]' or body is None:
            raise ValueError('Callback requires captured state')
        name = prefix+str(len(selected))
        if name.encode() in data:
            raise ValueError('Callback name collision: '+name)
        selected.append((node, body, name))
    if len(selected) != expected:
        raise ValueError('Captureless callback inventory changed')
    by_start = {node.start_byte: name for node, _, name in selected}
    def selected_parent(node):
        owner = node.parent
        while owner is not None:
            if owner.start_byte in by_start and owner.type == 'lambda_expression':
                return owner.start_byte
            owner = owner.parent
        return None
    edits = []
    definitions = ['static '+return_type+' '+name+parameters+';' for _,_,name in selected]
    for node, body, name in selected:
        value = body.text
        nested = [(child.start_byte-body.start_byte, child.end_byte-body.start_byte, child_name.encode())
                  for child,_,child_name in selected if selected_parent(child) == node.start_byte]
        for first,last,replacement in sorted(nested, reverse=True):
            value = value[:first]+replacement+value[last:]
        definitions.append('static '+return_type+' '+name+parameters+' '+value.decode())
        if selected_parent(node) is None:
            edits.append((node.start_byte, node.end_byte, name.encode()))
    for first, last, replacement in sorted(edits, reverse=True):
        data = data[:first]+replacement+data[last:]
    return data.decode(), '\n\n'.join(definitions)
