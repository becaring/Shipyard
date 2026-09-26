import unittest
from tree_sitter import Language, Parser
import tree_sitter_cpp
from xbox360_platform.prism import walk
from xbox360_platform.cpp_syntax import lift_captureless_callbacks


class CallbackTests(unittest.TestCase):
    def lower(self, text):
        return lift_captureless_callbacks(text, Parser(Language(tree_sitter_cpp.language())), walk,
            parameters='(int n)', return_type='void', prefix='Callback', expected=1)

    def test_preserves_body_and_other_signatures(self):
        text, definitions = self.lower('void f(){register_fn([](int n){sink(n);}); auto other=[](){done();};}')
        self.assertIn('register_fn(Callback0)', text)
        self.assertIn('auto other=[](){done();}', text)
        self.assertEqual(definitions, 'static void Callback0(int n) {sink(n);}')

    def test_rejects_capture_and_inventory_drift(self):
        for text in ('void f(){g([x](int n){sink(x+n);});}', 'void f(){}',
                     'void Callback0(); void f(){g([](int n){});} '):
            with self.assertRaises(ValueError):
                self.lower(text)
