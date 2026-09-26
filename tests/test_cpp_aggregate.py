import unittest
from tree_sitter import Language, Parser
import tree_sitter_cpp
from xbox360_platform.cpp_aggregate import lower_assignments
from xbox360_platform.prism import walk


class AggregateTests(unittest.TestCase):
    def setUp(self):
        self.parser = Parser(Language(tree_sitter_cpp.language()))

    def test_nested_array_and_conditional(self):
        result = lower_assignments('void f(){if(c) a={x(),{0},z}; else a={};}', self.parser, walk, {'a': ('Key', 2)})
        self.assertIn(' = {x(),{0},z}; a = ', result)
        self.assertIn('else { Key ', result)
        self.assertEqual(result.count('x()'), 1)
        self.assertFalse(self.parser.parse(result.encode()).root_node.has_error)

    def test_unreviewed_forms_fail(self):
        for source, targets in [('void f(){b={};}', {'a': ('Key', 1)}),
                                ('void f(){a={};}', {'a': ('Key', 2)}),
                                ('void f(){g(a={});}', {'a': ('Key', 1)})]:
            with self.assertRaises(ValueError):
                lower_assignments(source, self.parser, walk, targets)
