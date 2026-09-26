"""Lower reviewed scalar C booleans while preserving conversion to zero or one."""
from tree_sitter import Language, Parser
import tree_sitter_c


def lower(source: bytes, expected_types: int, *, approved_addresses=frozenset()) -> bytes:
    parser = Parser(Language(tree_sitter_c.language()))
    root = parser.parse(source).root_node
    nodes = []
    def visit(node):
        nodes.append(node)
        for child in node.named_children:
            visit(child)
    visit(root)
    types = [n for n in nodes if n.type in ('type_identifier', 'primitive_type') and n.text in (b'bool', b'_Bool')]
    if len(types) != expected_types:
        raise ValueError('Scalar boolean type inventory changed')
    variables = set()
    functions = []
    edits = []
    initializers = []
    address_uses = set()
    for node in types:
        owner = node.parent
        if owner.type == 'function_definition' and owner.child_by_field_name('type') == node:
            functions.append(owner)
        elif owner.type == 'declaration' and owner.child_by_field_name('type') == node:
            for declarator in owner.children_by_field_name('declarator'):
                name = declarator.child_by_field_name('declarator') if declarator.type == 'init_declarator' else declarator
                if name is not None and name.type == 'function_declarator':
                    function_name = name.child_by_field_name('declarator')
                    if function_name is None or function_name.type != 'identifier':
                        raise ValueError('Unsupported boolean function declarator')
                    continue
                if name is None or name.type != 'identifier':
                    raise ValueError('Unsupported non-scalar boolean declaration')
                variables.add(name.text)
                if declarator.type == 'init_declarator':
                    initializers.append(declarator.child_by_field_name('value'))
        else:
            raise ValueError('Unsupported boolean type context: ' + owner.type)
        edits.append((node.start_byte, node.end_byte, b'unsigned char'))

    def normalize(expression):
        if expression is None or expression.has_error:
            raise ValueError('Unparsed boolean conversion')
        edits.append((expression.start_byte, expression.start_byte, b'(('))
        edits.append((expression.end_byte, expression.end_byte, b') != 0)'))

    for value in initializers:
        normalize(value)
    for node in nodes:
        if node.type in ('declaration', 'parameter_declaration'):
            typ = node.child_by_field_name('type')
            if typ is not None and typ.text not in (b'bool', b'_Bool'):
                for declarator in node.children_by_field_name('declarator'):
                    name = declarator.child_by_field_name('declarator') if declarator.type == 'init_declarator' else declarator
                    while name is not None and name.type != 'identifier':
                        name = name.child_by_field_name('declarator')
                    if name is not None and name.type == 'identifier' and name.text in variables:
                        raise ValueError('Boolean identifier is shadowed by another type')
        if node.type == 'return_statement' and any(f.start_byte <= node.start_byte < f.end_byte for f in functions):
            if len(node.named_children) != 1:
                raise ValueError('Unparsed boolean return')
            normalize(node.named_children[0])
        if node.type == 'assignment_expression':
            left = node.child_by_field_name('left')
            if left is not None and left.type == 'identifier' and left.text in variables:
                operator = node.child_by_field_name('operator')
                if operator is None or operator.text != b'=':
                    raise ValueError('Compound boolean assignment requires separate lowering')
                normalize(node.child_by_field_name('right'))
        if node.type in ('update_expression', 'pointer_expression'):
            for child in node.named_children:
                if child.type != 'identifier' or child.text not in variables:
                    continue
                if node.type == 'pointer_expression' and node.text.startswith(b'&') and child.text in approved_addresses:
                    address_uses.add(child.text)
                else:
                    raise ValueError('Boolean address or increment requires separate lowering')
    if address_uses != approved_addresses:
        raise ValueError('Approved boolean address inventory changed')
    for start, end, value in sorted(set(edits), reverse=True):
        source = source[:start] + value + source[end:]
    return source
