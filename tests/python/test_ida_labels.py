"""The IDA names round trip's planning (labels in, your names out), without IDA."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'reagentlive', 'frontends', 'ida'))
import reagentlive_ida as labels  # noqa: E402

ROW = '{"kind":"%s","module":"Game.exe","module_size":4096,"name":"%s","rva":"%s","timestamp":7}'


class ImportLabels(unittest.TestCase):
    def test_load_skips_malformed_lines(self):
        rows = labels.load([ROW % ('exec', 'Actor::execGetSpeed', '0x10'), 'not json', '{"rva":"zz"}', ''])
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['rva'], 0x10)

    def test_the_build_comes_from_the_pe_header(self):
        header = bytearray(0x58)
        header[:4] = b'PE\0\0'
        header[8:12] = (7).to_bytes(4, 'little')          # TimeDateStamp
        header[0x50:0x54] = (4096).to_bytes(4, 'little')  # SizeOfImage
        self.assertEqual(labels.build_of(bytes(header)), (4096, 7))
        self.assertIsNone(labels.build_of(b''))
        self.assertIsNone(labels.build_of(b'\xff' * 0x58))  # Not a header: bytes IDA never loaded.
        self.assertIsNone(labels.build_of(bytes(header[:0x40])))

    def test_only_this_builds_rows_are_used(self):
        mine, other = ROW % ('exec', 'A::execB', '0x10'), ROW.replace('"timestamp":7', '"timestamp":8') % ('exec', 'C::execD', '0x20')
        rows = labels.of_build(labels.load([mine, other]), 4096, 7)
        self.assertEqual([row['name'] for row in rows], ['A::execB'])      # One file holds several builds.
        self.assertIn('no labels for this build', labels.of_build(labels.load([other]), 4096, 7))
        self.assertIn('no labels', labels.of_build([], 4096, 7))

    def test_a_users_name_outranks_reflected_ones(self):
        rows = labels.load([ROW % ('exec', 'A::execB', '0x10'), ROW % ('user', 'Ricochet_Resolve', '0x10'),
                            ROW % ('vtable', 'vtbl_A', '0x20'), ROW % ('global', 'GTag', '0x20')])
        self.assertEqual([(row['rva'], row['name']) for row in labels.best(rows)],
                         [(0x10, 'Ricochet_Resolve'), (0x20, 'GTag')])

    def test_your_names_go_out_as_user_rows_added_or_updated(self):
        build = {'module': 'Game.exe', 'module_size': 4096, 'timestamp': 7, 'base': 0x140000000}
        rows = labels.user_rows([(0x6fa0aa0, 'Ricochet_Resolve'), (0x10, 'fn_10'), (0x20, 'sub_140000020'),
                                 (0x30, 'stub_140000030'), (0x40, 'loc_40')], build)
        self.assertEqual([(row['rva'], row['name'], row['kind']) for row in rows], [('0x6fa0aa0', 'Ricochet_Resolve', 'user')])
        renamed = ROW % ('user', 'OldName', '0x6fa0aa0')
        elsewhere = ROW % ('user', 'FromAnotherDatabase', '0x50')
        other_build = ROW.replace('"timestamp":7', '"timestamp":8') % ('user', 'Kept', '0x60')
        reflected = ROW % ('exec', 'A::execB', '0x10')
        lines = labels.merge_user_rows([renamed, elsewhere, other_build, reflected, 'not json'], rows, build)
        self.assertNotIn(renamed, lines)                    # Same address: updated.
        self.assertIn(elsewhere, lines)                     # A name this database does not hold: kept.
        self.assertIn(other_build, lines)
        self.assertIn(reflected, lines)
        self.assertEqual(labels.load(lines[-1:])[0]['name'], 'Ricochet_Resolve')
        self.assertEqual(len(lines), 4)                     # The malformed line is dropped.
        self.assertEqual(labels.merge_user_rows(lines, [], build), lines)   # Nothing of yours: nothing lost.

    def test_a_users_name_stays_and_the_label_becomes_a_comment(self):
        rows = labels.load([ROW % ('exec', 'A::execB', '0x10'), ROW % ('vtable', 'vtbl_A', '0x20'),
                            ROW % ('global', 'FName_Tag.With Space', '0x30'), ROW % ('exec', 'Same', '0x40')])
        existing = {0x10: ('MyName', True), 0x20: ('sub_20', False), 0x40: ('Same', False)}
        actions = labels.plan(rows, lambda rva: existing.get(rva, ('', False)))
        self.assertEqual(actions, [('comment', 0x10, 'reagentlive: A::execB'), ('name', 0x20, 'vtbl_A', 'vtable'),
                                   ('name', 0x30, 'FName_Tag.With_Space', 'global'), ('skip', 0x40)])


if __name__ == '__main__':
    unittest.main()
