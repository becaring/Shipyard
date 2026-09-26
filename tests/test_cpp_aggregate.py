import unittest
from tree_sitter import Language, Parser
import tree_sitter_cpp
from xbox360_platform.cpp_aggregate import lower_assignments, lower_macro_arguments
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

    def test_initializer_macro_argument_preserves_conditional_scope(self):
        source = 'void f(){if(c) patch(1, MAKE(next())); else patch(2, saved);}'
        result = lower_macro_arguments(source, self.parser, walk, function='patch', argument=1,
                                       macros=('MAKE',), type_name='Pair', expected=1)
        self.assertEqual(result.count('next()'), 1)
        self.assertIn('if(c) { Pair _aggregateArgument = MAKE(next()); patch(1, _aggregateArgument); } else', result)
        self.assertFalse(self.parser.parse(result.encode()).root_node.has_error)

    def test_initializer_macro_argument_rejects_unknown_forms(self):
        for source in ('void f(){return patch(1, MAKE(2));}', 'void f(){patch(1, OTHER(2));}',
                       'void f(){patch(1);}', 'void f(){patch(1, saved);}'):
            with self.assertRaises(ValueError):
                lower_macro_arguments(source, self.parser, walk, function='patch', argument=1,
                                      macros=('MAKE',), type_name='Pair', expected=1)
