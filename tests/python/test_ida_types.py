"""The IDA loader's decompile_to naming and grep, and the layout type builder, without IDA."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'reagentlive', 'frontends', 'ida'))
import reagentlive_ida as rl  # noqa: E402


class DecompileTo(unittest.TestCase):
    def test_file_names_are_safe_and_unique(self):
        names = rl.file_names([(0x1000, 'AThing::Tick'), (0x2000, 'athing__tick'), (0x3000, ''),
                                   (0x4000, 'A?B<int>'), (0x5000, 'x' * 200)])
        self.assertEqual(names[0], 'AThing__Tick.c')
        self.assertEqual(names[1], 'athing__tick_2000.c')   # Taken already, ignoring case.
        self.assertEqual(names[2], 'fn_3000.c')
        self.assertEqual(names[3], 'A_B_int.c')
        self.assertEqual(names[4], 'x' * 120 + '.c')

    def test_grep_lists_matching_lines_by_file_line(self):
        text = 'void f(Thing *this)\n{\n  this->bArmed = 1;\n  g(this->Speed);\n}\n'
        entry = rl.summary(0x1000, 'f', 'out/f.c', text, rl.matcher(r'this->\w+'))
        self.assertEqual(entry['lines'], 5)
        self.assertEqual(entry['matches'], ['3: this->bArmed = 1;', '4: g(this->Speed);'])
        self.assertNotIn('error', entry)
        plain = rl.summary(0x1000, 'f', 'out/f.c', text, rl.matcher('g(this'))   # No valid regex: a substring.
        self.assertEqual(plain['matches'], ['4: g(this->Speed);'])
        self.assertNotIn('matches', rl.summary(0x1000, 'f', 'out/f.c', text, rl.matcher(None)))

    def test_matches_are_capped(self):
        entry = rl.summary(0x1000, 'f', 'f.c', 'x\n' * 50, rl.matcher('x'))
        self.assertEqual(len(entry['matches']), 40)
        self.assertEqual(entry['matches_more'], 10)

    def test_a_failed_decompile_reports_its_error(self):
        entry = rl.summary(0x1000, 'f', 'f.c', '// decompile failed: boom\n', None, 'boom')
        self.assertEqual(entry['error'], 'boom')
        self.assertEqual(entry['lines'], 1)


def struct_of(text, name):
    """(head, members) of `struct name ... { ... };` in declaration text."""
    for line in text.splitlines():
        if line.startswith((f'struct {name} {{', f'struct {name} :')):
            head, _, body = line.partition(' { ')
            return head, [m.strip() for m in body[:-3].split(';') if m.strip()]
    return None


# As layout-export --closure writes it: a type line, its own fields by offset; enums last.
LAYOUT = [
    '{"kind":"class","name":"Thing","super":"Base","size":"0x70"}',
    '{"kind":"field","owner":"Thing","field":"Speed","offset":"0x30","size":4,"type":"FloatProperty"}',
    '{"kind":"field","owner":"Thing","field":"bArmed","offset":"0x34","size":1,"type":"BoolProperty","bool_mask":1}',
    '{"kind":"field","owner":"Thing","field":"bLive","offset":"0x34","size":1,"type":"BoolProperty","bool_mask":2}',
    '{"kind":"field","owner":"Thing","field":"Mode","offset":"0x35","size":1,"type":"ByteProperty","enum":"EMode"}',
    '{"kind":"field","owner":"Thing","field":"Where","offset":"0x38","size":8,"type":"StructProperty","struct":"Pair"}',
    '{"kind":"field","owner":"Thing","field":"Items","offset":"0x40","size":16,"type":"ArrayProperty",'
    '"element":{"type":"ObjectProperty","size":8,"class":"Base"}}',
    '{"kind":"field","owner":"Thing","field":"Maps","offset":"0x50","size":16,"type":"ArrayProperty",'
    '"element":{"type":"MapProperty","size":80}}',
    '{"kind":"field","owner":"Thing","field":"Tag","offset":"0x60","size":8,"type":"NameProperty"}',
    '{"kind":"field","owner":"Thing","field":"Corners","offset":"0x68","size":4,"type":"Int16Property"}',
    '',
    '{"kind":"class","name":"Base","size":"0x28"}',
    '{"kind":"field","owner":"Base","field":"Owner","offset":"0x20","size":8,"type":"ObjectProperty","class":"Base"}',
    '{"kind":"struct","name":"Pair","size":"0x8"}',
    '{"owner":"Pair","field":"A","offset":"0x0","size":4,"type":"IntProperty"}',
    '{"kind":"field","owner":"Pair","field":"B","offset":"0x4","size":4,"type":"FloatProperty"}',
    '{"kind":"enum","name":"EMode","size":1,"values":[["EMode::Off",0],["EMode::On",2],["EMode::Big",300]]}',
    '{"kind":"enum","name":"EOther","values":[["EMode::Off",0],["EOther::Neg",-1]]}',
    '{"kind":"widget","name":"x"}',
    'not json',
]


class Types(unittest.TestCase):
    def build(self, lines=LAYOUT):
        classes, enums, read = rl.layouts(lines)
        text, report = rl.declarations(classes, enums)
        return text, report, read

    def test_a_layout_file_reads_into_types_and_enums(self):
        classes, enums, read = rl.layouts(LAYOUT)
        self.assertEqual(list(classes), ['Thing', 'Base', 'Pair'])
        self.assertEqual((classes['Thing']['base'], classes['Thing']['size']), ('Base', 0x70))
        self.assertEqual(classes['Pair']['fields'][0]['offset'], 0)          # A line without kind is a field.
        self.assertEqual(enums['EMode']['values'][1], ('EMode::On', 2))
        self.assertEqual(len(read['lines_skipped']), 2)                      # The unknown kind, the bad line.
        self.assertIn('unknown kind "widget"', read['lines_skipped'][0])
        self.assertIsInstance(rl.layouts(['', 'nothing']), str)

    def test_types_become_packed_structs_with_their_super_as_base(self):
        text, report, _ = self.build()
        lines = text.splitlines()
        self.assertEqual((lines[0], lines[-1]), ('#pragma pack(push, 1)', '#pragma pack(pop)'))
        self.assertEqual(struct_of(text, 'Thing'), ('struct Thing : Base', [
            'unsigned __int8 pad_28[0x8]', 'float Speed', 'unsigned __int8 bArmed__bLive', 'enum EMode Mode',
            'unsigned __int8 pad_36[0x2]', 'struct Pair Where', 'struct TArray_Base_ptr Items',
            'struct TArray Maps', 'struct FName Tag', '__int16 Corners[2]', 'unsigned __int8 pad_6c[0x4]']))
        self.assertEqual(struct_of(text, 'Base'), ('struct Base', ['unsigned __int8 pad_0[0x20]', 'struct Base *Owner']))
        self.assertIn('struct TArray_Base_ptr { struct Base **Data; __int32 Num; __int32 Max; };', text)
        self.assertEqual(report['arrays'], ['TArray_Base_ptr'])
        self.assertEqual(report['untyped'], [])

    def test_held_types_come_first_and_every_type_is_forward_declared(self):
        text, report, _ = self.build()
        self.assertEqual(report['types'], ['Base', 'Pair', 'Thing'])   # Super and struct fields before their holder.
        self.assertIn('struct Thing;', text)
        self.assertLess(text.index('struct TArray_Base_ptr {'), text.index('struct Thing : Base'))
        self.assertIn('struct FName { __int32 ComparisonIndex; __int32 Number; };', text)

    def test_enums_keep_their_width_and_unique_member_names(self):
        text, report, _ = self.build()
        self.assertIn('enum EMode : unsigned __int8 { EMode__Off = 0x0, EMode__On = 0x2 };', text)
        self.assertIn('enum EOther : unsigned __int8 { EOther__EMode__Off = 0x0, EOther__Neg = -1 };', text)
        self.assertEqual(report['enums'], ['EMode', 'EOther'])
        self.assertEqual(report['skipped'], ['EMode.EMode::Big: 300 does not fit 1 bytes'])

    def test_an_enum_without_size_takes_its_fields(self):
        lines = ['{"kind":"class","name":"One","size":"0x4"}',
                 '{"kind":"field","owner":"One","field":"E","offset":"0x0","size":4,"type":"EnumProperty","enum":"EWide"}',
                 '{"kind":"enum","name":"EWide","values":[["A",0]]}']
        text, _, _ = self.build(lines)
        self.assertIn('enum EWide : unsigned __int32 { A = 0x0 };', text)
        self.assertEqual(struct_of(text, 'One'), ('struct One', ['enum EWide E']))

    def test_unknown_targets_fall_back(self):
        lines = ['{"kind":"class","name":"One","super":"Missing","size":"0x48"}',
                 '{"kind":"field","owner":"One","field":"Ref","offset":"0x28","size":8,"type":"ObjectProperty","class":"Elsewhere"}',
                 '{"kind":"field","owner":"One","field":"Blob","offset":"0x30","size":12,"type":"StructProperty","struct":"Elsewhere"}',
                 '{"kind":"field","owner":"One","field":"Wide","offset":"0x38","size":4,"type":"IntProperty"}',
                 '{"kind":"field","owner":"One","field":"class","offset":"0x3c","size":1,"type":"EnumProperty","enum":"ENotHere"}',
                 '{"kind":"field","owner":"One","field":"Set","offset":"0x40","size":8,"type":"SetProperty"}']
        text, report, _ = self.build(lines)
        self.assertEqual(struct_of(text, 'One'), ('struct One', [
            'unsigned __int8 pad_0[0x28]', 'void *Ref', 'unsigned __int8 Blob[0xc]', 'unsigned __int8 _class',
            'unsigned __int8 pad_3d[0x3]', 'unsigned __int8 Set[0x8]']))
        self.assertEqual(report['skipped'], ['One: super Missing not in the file, padded',
                                             'One.Wide: overlaps the member before (at 0x3c)'])
        self.assertEqual(report['untyped'], ['One.Blob: struct Elsewhere not in the file',
                                             'One.Set: SetProperty: no C type'])

    def test_lines_without_class_or_enum_keys_stay_typed(self):
        # An older agent writes no `class` or `enum` keys: pointers stay pointers, enums their integer.
        lines = ['{"kind":"class","name":"One","size":"0x28"}',
                 '{"kind":"field","owner":"One","field":"Ref","offset":"0x0","size":8,"type":"ObjectProperty"}',
                 '{"kind":"field","owner":"One","field":"Kind","offset":"0x8","size":1,"type":"ByteProperty"}',
                 '{"kind":"field","owner":"One","field":"Mode","offset":"0x9","size":1,"type":"EnumProperty"}',
                 '{"kind":"field","owner":"One","field":"Wide","offset":"0xc","size":4,"type":"EnumProperty"}',
                 '{"kind":"field","owner":"One","field":"Refs","offset":"0x10","size":16,"type":"ArrayProperty",'
                 '"element":{"type":"ObjectProperty","size":8}}',
                 '{"kind":"field","owner":"One","field":"' + 'y' * 95 + '","offset":"0x20","size":1,"type":"BoolProperty","bool_mask":1}',
                 '{"kind":"field","owner":"One","field":"' + 'x' * 60 + '","offset":"0x20","size":1,"type":"BoolProperty","bool_mask":2}',
                 '{"kind":"enum","name":"ENamed","values":[["A",0],["A_MAX",1]]}']
        text, report, _ = self.build(lines)
        self.assertEqual(struct_of(text, 'One'), ('struct One', [
            'void *Ref', 'unsigned __int8 Kind', 'unsigned __int8 Mode', 'unsigned __int8 pad_a[0x2]',
            'unsigned __int32 Wide', 'struct TArray_void_ptr Refs', 'unsigned __int8 ' + 'y' * 95,   # Cut to 96, no dangling '_'.
            'unsigned __int8 pad_21[0x7]']))
        self.assertIn('enum ENamed : unsigned __int8 { A = 0x0, A_MAX = 0x1 };', text)
        self.assertEqual((report['untyped'], report['skipped']), ([], []))

    def test_a_map_key_or_value_sizes_its_enum(self):
        lines = ['{"kind":"class","name":"One","size":"0x50"}',
                 '{"kind":"field","owner":"One","field":"Roles","offset":"0x0","size":80,"type":"MapProperty",'
                 '"key":{"type":"ObjectProperty","size":8,"class":"One"},'
                 '"value":{"type":"EnumProperty","size":4,"enum":"ERole"},"stride":24,"value_offset":"0x8"}',
                 '{"kind":"enum","name":"ERole","values":[["R",0]]}']
        text, report, _ = self.build(lines)
        self.assertIn('enum ERole : unsigned __int32 { R = 0x0 };', text)
        self.assertEqual(struct_of(text, 'One'), ('struct One', ['unsigned __int8 Roles[0x50]']))
        self.assertEqual(report['untyped'], ['One.Roles: MapProperty: no C type'])

    def test_a_method_label_names_its_class_by_the_longest_known_prefix(self):
        known = {'Thing', 'Thing_Sub'}.__contains__
        self.assertEqual(rl.this_class('Thing_Sub_Tick', known), 'Thing_Sub')
        self.assertEqual(rl.this_class('Thing_Tick', known), 'Thing')
        self.assertEqual(rl.this_class('Thing::execTick', known), 'Thing')
        self.assertIsNone(rl.this_class('Other_Tick', known))
        self.assertIsNone(rl.this_class('Thing', known))   # No method part.


# A save with layout rows (disasm save of a labelled method): the console adds "row": "layout".
SAVE = [
    '{"base":"0x140000000","end":"0x140001006","module":"Game.exe","module_size":4096,"row":"function",'
    '"start":"0x140001000","timestamp":7}',
    '{"base":"0x140000000","end":"0x140002001","module":"Game.exe","module_size":4096,"row":"function",'
    '"start":"0x140002000","timestamp":7}',
    '{"address":"0x140001000","bytes":"8b4130c3c3c3","row":"code","rva":"0x1000"}',
    '{"address":"0x140002000","bytes":"c3","row":"code","rva":"0x2000"}',
    '{"address":"0x140001000","name":"Thing_Tick","row":"name","rva":"0x1000","what":"function"}',
    '{"address":"0x140002000","name":"fn_2000","row":"name","rva":"0x2000","what":"function"}',
    '{"address":"0x140003000","name":"Thing_Other","row":"name","rva":"0x3000","what":"call"}',
] + ['{"row":"layout",' + line[1:] for line in LAYOUT[:16] if line]


class SavedTypes(unittest.TestCase):
    def test_layout_rows_read_as_a_layout_file(self):
        plan = rl.parse(SAVE)
        self.assertIsInstance(plan, dict)
        self.assertEqual(len(plan['layouts']), 15)
        classes, enums, report = rl.layouts(plan['layouts'])
        self.assertEqual(set(classes), {'Thing', 'Base', 'Pair'})
        self.assertEqual(set(enums), set())   # LAYOUT's enum lines come later.
        self.assertEqual(report['lines_skipped'], [])
        self.assertEqual(classes['Thing']['base'], 'Base')

    def test_methods_are_the_saved_functions_with_names(self):
        plan = rl.parse(SAVE)
        self.assertEqual(rl.methods(plan), [(0x140001000, 'Thing_Tick'), (0x140002000, 'fn_2000')])

    def test_saved_types_declares_then_types_this_with_what_a_load_left(self):
        plan = rl.parse(SAVE)
        calls = []
        saved = (rl.loader._declare, rl.loader._type_this, rl.loader._take_pending, rl.loader._defer_this)
        rl.loader._declare = lambda classes, enums, report: calls.append(('declare', sorted(classes))) or             {'types': sorted(classes), 'enums': [], 'failed': []}
        rl.loader._type_this = lambda named: calls.append(('this', named)) or             {'typed': 1, 'skipped': ['fn_2000: x'], 'left': [(0x140005000, 'Thing_Late')]}
        rl.loader._take_pending = lambda: [(0x140004000, 'Thing_Earlier')]
        rl.loader._defer_this = lambda named: calls.append(('defer', named))
        try:
            now = rl.saved_types(plan)
            self.assertEqual(now, {'types': 3, 'types_failed': [], 'this_typed': 1,
                                   'this_skipped': ['fn_2000: x', 'Thing_Late: no decompiler']})
            self.assertEqual(calls[1], ('this', [(0x140004000, 'Thing_Earlier'), (0x140001000, 'Thing_Tick'),
                                                 (0x140002000, 'fn_2000')]))
            calls.clear()
            later = rl.saved_types(plan, now=False)   # While loading: the types, `this` waits.
            self.assertEqual(later['this_typed'], 0)
            self.assertEqual([call[0] for call in calls], ['declare', 'defer'])
            self.assertIn('error', rl.saved_types(dict(plan, layouts=['not json'])))
        finally:
            rl.loader._declare, rl.loader._type_this, rl.loader._take_pending, rl.loader._defer_this = saved

    def test_layout_rows_without_a_type_are_no_layout(self):
        plan = rl.parse(SAVE[:4] + ['{"row":"layout","kind":"widget","name":"x"}'])
        self.assertEqual(rl.layouts(plan['layouts']), 'no layout lines: not a layout-export file')


if __name__ == '__main__':
    unittest.main()
