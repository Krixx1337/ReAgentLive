"""The IDA label import's planning, without IDA."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'reagentlive', 'frontends', 'ida'))
import import_labels as labels  # noqa: E402

ROW = '{"kind":"%s","module":"Game.exe","module_size":4096,"name":"%s","rva":"%s","timestamp":7}'


class ImportLabels(unittest.TestCase):
    def test_load_skips_malformed_lines(self):
        rows = labels.load([ROW % ('exec', 'Actor::execGetSpeed', '0x10'), 'not json', '{"rva":"zz"}', ''])
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['rva'], 0x10)

    def test_another_build_is_refused(self):
        rows = labels.load([ROW % ('exec', 'A::execB', '0x10')])
        self.assertIsNone(labels.check_build(rows, 4096, 7))
        self.assertIn('another build', labels.check_build(rows, 4096, 8))
        self.assertIn('no labels', labels.check_build([], 4096, 7))

    def test_a_users_name_stays_and_the_label_becomes_a_comment(self):
        rows = labels.load([ROW % ('exec', 'A::execB', '0x10'), ROW % ('vtable', 'vtbl_A', '0x20'),
                            ROW % ('global', 'FName_Tag.With Space', '0x30'), ROW % ('exec', 'Same', '0x40')])
        existing = {0x10: ('MyName', True), 0x20: ('sub_20', False), 0x40: ('Same', False)}
        actions = labels.plan(rows, lambda rva: existing.get(rva, ('', False)))
        self.assertEqual(actions, [('comment', 0x10, 'reagentlive: A::execB'), ('name', 0x20, 'vtbl_A', 'vtable'),
                                   ('name', 0x30, 'FName_Tag.With_Space', 'global'), ('skip', 0x40)])


if __name__ == '__main__':
    unittest.main()
