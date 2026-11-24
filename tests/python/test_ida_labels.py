"""The IDA names round trip's planning (labels in, your names out), without IDA."""
import contextlib
import io
import os
import sys
import tempfile
import types
import unittest
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'reagentlive', 'frontends', 'ida'))
from reagentlive_ida import labels  # noqa: E402

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
                                 (0x30, 'stub_140000030'), (0x40, 'loc_40'), (0x41, 'def_141B58BB9'),
                                 (0x42, 'jpt_141B58BB9'), (0x43, '__alloca_probe'), (0x44, '??_7Actor@@6B@'),
                                 (0x45, 'jumptable 0000000141B58BB9 case 3'), (0x46, 'unk_141B58BB9'),
                                 (0x47, 'stru_14000F000'), (0x48, '$LN5'), (0x49, '')], build)
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

    def test_a_rows_signature_survives_a_rename(self):
        build = {'module': 'Game.exe', 'module_size': 4096, 'timestamp': 7, 'base': 0x140000000}
        signed = ROW[:-1] % ('user', 'OldName', '0x10') + ',"sig":{"pattern":"48 8b c4"},"ported_from":6}'
        rows = labels.user_rows([(0x10, 'NewName')], build)
        merged = labels.load(labels.merge_user_rows([signed], rows, build))
        self.assertEqual(len(merged), 1)
        self.assertEqual(merged[0]['name'], 'NewName')     # The database's name wins...
        self.assertEqual(merged[0]['sig'], {'pattern': '48 8b c4'})  # ...the signature follows the address.
        self.assertEqual(merged[0]['ported_from'], 6)

    def test_export_skips_library_and_ida_names(self):
        base = 0x140000000
        names = {base + 0x10: 'Ricochet_Resolve', base + 0x20: 'memcpy_impl', base + 0x30: '_guard_check',
                 base + 0x40: 'sub_140000040', base + 0x50: 'def_141B58BB9', base + 0x60: 'jpt_141B58BB9',
                 base + 0x70: '__alloca_probe', base + 0x80: 'AutoNamed', base + 0x90: 'Rejected'}
        library_functions = {base + 0x20}      # A FLIRT match: its function carries FUNC_LIB.
        library_items = {base + 0x30}          # A library item outside a library function.
        auto_names = {base + 0x80}             # IDA's own name, though flagged as set.

        class Function:
            def __init__(self, ea):
                self.flags = 4 if ea in library_functions else 0

        fakes = {
            'ida_bytes': types.SimpleNamespace(get_flags=lambda ea: ea, has_user_name=lambda flags: True,
                                               has_auto_name=lambda flags: flags in auto_names),
            'ida_funcs': types.SimpleNamespace(FUNC_LIB=4, get_func=Function),
            'ida_kernwin': types.SimpleNamespace(),
            'ida_nalt': types.SimpleNamespace(is_libitem=lambda ea: ea in library_items),
            'ida_name': types.SimpleNamespace(is_uname=lambda name: name != 'Rejected'),
            'idautils': types.SimpleNamespace(Names=lambda: sorted(names.items())),
        }
        build = {'module': 'Game.exe', 'module_size': 4096, 'timestamp': 7, 'base': base}
        with tempfile.TemporaryDirectory() as folder, \
                mock.patch.dict(sys.modules, fakes), mock.patch.object(labels, '_database_build', lambda: build):
            path = os.path.join(folder, 'labels.jsonl')
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(labels.export_names(path), 1)
            with open(path, encoding='utf-8') as handle:
                self.assertEqual([row['name'] for row in labels.load(handle)], ['Ricochet_Resolve'])

    def test_the_import_line_says_why_nothing_was_applied(self):
        counts = {'named': 0, 'commented': 0, 'outside': 25860, 'already': 5, 'rows': 25865, 'cancelled': False}
        line = labels.labels_message(counts)
        self.assertIn('25865 skipped', line)
        self.assertIn("25860 outside the database's loaded bytes", line)
        self.assertIn('5 named or commented so already', line)
        self.assertIn('a save database holds only its saved functions', line)
        self.assertNotIn('nothing to name', labels.labels_message(dict(counts, named=4)))
        self.assertEqual(labels.labels_message({'named': 2, 'commented': 1, 'outside': 0, 'already': 0, 'rows': 3}),
                         'ReAgentLive labels: 2 named, 1 commented, 0 skipped, from 3 rows of this build')

    def test_a_users_name_stays_and_the_label_becomes_a_comment(self):
        rows = labels.load([ROW % ('exec', 'A::execB', '0x10'), ROW % ('vtable', 'vtbl_A', '0x20'),
                            ROW % ('global', 'FName_Tag.With Space', '0x30'), ROW % ('exec', 'Same', '0x40')])
        existing = {0x10: ('MyName', True), 0x20: ('sub_20', False), 0x40: ('Same', False)}
        actions = labels.plan(rows, lambda rva: existing.get(rva, ('', False)))
        self.assertEqual(actions, [('comment', 0x10, 'reagentlive: A::execB'), ('name', 0x20, 'vtbl_A', 'vtable'),
                                   ('name', 0x30, 'FName_Tag.With_Space', 'global'), ('skip', 0x40)])


if __name__ == '__main__':
    unittest.main()
