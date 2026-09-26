import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
from xbox360_platform.cpp_syntax import collect_member_defaults, lower_if_initializers


class Node:
    def __init__(self, kind, data, start=0, end=None, fields=None, children=None, field_lists=None):
        self.type = kind
        self.start_byte = start
        self.end_byte = len(data) if end is None else end
        self.text = data[self.start_byte:self.end_byte]
        self.fields = fields or {}
        self.named_children = children or []
        self.field_lists = field_lists or {}

    def child_by_field_name(self, name):
        return self.fields.get(name)

    def children_by_field_name(self, name):
        return self.field_lists.get(name, [])


class MemberParser:
    def parse(self, data):
        fields = []
        for typ, declaration, name, default in (
            ('int', 'int count = 4;', 'count', '4'),
            ('float', 'float scales[2] = {1.0f, 2.0f};', 'scales', '{1.0f, 2.0f}'),
            ('bool', 'bool enabled = true;', 'enabled', 'true'),
        ):
            start = data.index(declaration.encode())
            typ_start = data.index(typ.encode(), start)
            name_start = data.index(name.encode(), typ_start)
            default_start = data.index(default.encode(), name_start)
            identifier = Node('field_identifier', data, name_start, name_start + len(name))
            declarator_end = name_start + len(name)
            is_array = name == 'scales'
            if is_array:
                declarator_end += len('[2]')
            declarator = Node('array_declarator' if is_array else 'field_identifier', data,
                              name_start, declarator_end, children=[identifier] if is_array else [])
            value = Node('initializer_list' if default.startswith('{') else 'number_literal',
                         data, default_start, default_start + len(default))
            fields.append(Node('field_declaration', data, start, start + len(declaration),
                               fields={'type': Node('primitive_type', data, typ_start, typ_start + len(typ))},
                               field_lists={'declarator': [declarator], 'default_value': [value]}))
        name = Node('type_identifier', data, data.index(b'State'), data.index(b'State') + 5)
        body = Node('field_declaration_list', data, children=fields)
        cls = Node('struct_specifier', data, fields={'name': name, 'body': body}, children=[body])
        return type('Tree', (), {'root_node': Node('root', data, children=[cls])})()


class Parser:
    def parse(self, data):
        if data.startswith(b'{ '):
            return type('Tree', (), {'root_node': Node('root', data)})()
        initializer = Node('declaration', data, 4, 18)
        value = Node('identifier', data, 19, 24)
        condition = Node('condition', data, 4, 25, {
            'initializer': initializer, 'value': value,
        })
        statement = Node('if_statement', data, fields={'condition': condition})
        root = Node('root', data, children=[statement])
        return type('Tree', (), {'root_node': root})()


def walk(node):
    yield node
    for child in node.named_children:
        yield from walk(child)


class SyntaxTests(unittest.TestCase):
    def test_inline_override_is_not_a_member_default(self):
        from tree_sitter import Language, Parser as RealParser
        import tree_sitter_cpp
        parser = RealParser(Language(tree_sitter_cpp.language()))
        source = 'struct State { void Draw() override{}; int count = 4; };'
        lowered, fields = collect_member_defaults(source, 'State', parser, walk)
        self.assertIn('void Draw() override{};', lowered)
        self.assertEqual([(field.name, field.value) for field in fields], [('count', '4')])
        with self.assertRaisesRegex(ValueError, 'Unsupported member default'):
            collect_member_defaults('struct State { void (*callback)() = nullptr; };', 'State', parser, walk)

    def test_member_defaults(self):
        source = 'struct State { int count = 4; float scales[2] = {1.0f, 2.0f}; bool enabled = true; };'
        lowered, fields = collect_member_defaults(source, 'State', MemberParser(), walk)
        self.assertEqual(lowered, 'struct State { int count; float scales[2]; bool enabled; };')
        self.assertEqual([(field.type, field.name, field.value, field.is_array) for field in fields], [
            ('int', 'count', '4', False),
            ('float', 'scales', '{1.0f, 2.0f}', True),
            ('bool', 'enabled', 'true', False),
        ])

    def test_if_initializer(self):
        source = 'if (int value = 1; value) work();'
        self.assertEqual(
            lower_if_initializers(source, Parser(), walk, expected=1),
            '{ int value = 1; if (value) work(); }',
        )

    def test_inventory_drift(self):
        with self.assertRaisesRegex(ValueError, 'inventory drift'):
            lower_if_initializers('if (int value = 1; value) work();', Parser(), walk, expected=2)


if __name__ == '__main__':
    unittest.main()
