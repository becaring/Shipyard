import unittest
from tree_sitter import Language,Parser
import tree_sitter_c
from xbox360_platform.c_designated_tables import lower
from xbox360_platform.prism import walk


class DesignatedTableTests(unittest.TestCase):
    def setUp(self):
        self.parser = Parser(Language(tree_sitter_c.language()))

    def project(self, source):
        return lower(source.encode(),self.parser,walk,records={'config':'struct Record'},tables={'table':'Entry'},
            schemas={'Entry':('first','second'),'struct Record':('flag','data','size')},indices={'A':0,'C':2})

    def test_sparse_extent_field_order_and_omitted_zero(self):
        source = 'Entry table[] = {[C]={.second=7,.first=9},[A]={.second=2}}; struct Record config={.size=8,.flag=1};'
        result = self.project(source)
        self.assertIn(b'{\n{ 0, 2 },\n{ 0 },\n{ 9, 7 }\n}',result)
        self.assertIn(b'config={ 1, 0, 8 }',result)
        self.assertFalse(self.parser.parse(result).root_node.has_error)

    def test_unknown_duplicate_mixed_and_explicit_extent_rejected(self):
        record = ' struct Record config={.flag=1};'
        for table in ('Entry table[]={[A]={.unknown=1}};',
                      'Entry table[]={[A]={.first=1,.first=2}};',
                      'Entry table[]={[A]={.first=1},[A]={.first=2}};',
                      'Entry table[]={[B]={.first=1}};',
                      'Entry table[]={[A]={1,2}};',
                      'Entry table[8]={[A]={.first=1}};'):
            with self.subTest(table=table),self.assertRaises(ValueError):
                self.project(table+record)

    def test_preprocessor_line_markers_do_not_change_table_extent(self):
        source = 'Entry table[] = {\n[A]={.second=2},\n#line 27 "table.c"\n[C]={.first=9}\n}; struct Record config={.flag=1};'
        result = self.project(source)
        self.assertIn(b'{\n{ 0, 2 },\n{ 0 },\n{ 9, 0 }\n}',result)
