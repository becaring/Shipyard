import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'python'))
from xbox360_platform.cpp_syntax import lower_if_initializers


class Node:
    def __init__(self, kind, data, start=0, end=None, fields=None, children=None):
        self.type = kind
        self.start_byte = start
        self.end_byte = len(data) if end is None else end
        self.text = data[self.start_byte:self.end_byte]
        self.fields = fields or {}
        self.named_children = children or []

    def child_by_field_name(self, name):
        return self.fields.get(name)


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
