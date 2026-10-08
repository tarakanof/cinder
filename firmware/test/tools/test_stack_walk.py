import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
import stack_walk

DISASM = """
42000000 <entry_fn>:
42000000:\tentry\ta1, 32
42000003:\tl32r\ta8, 41ffff00 <_lit> (42000100 <leaf>)
42000006:\tcallx8\ta8
42000009:\tl32r\ta8, 41ffff04 <_lit+0x4> (3fc90000 <s_data>)
4200000c:\tcallx8\ta8
4200000f:\tl32r\ta9, 41ffff08 <_lit+0x8> (42000100 <leaf+0x4>)
42000012:\tcallx8\ta9
42000015:\tcall8\t42000200 <dispatch>
42000018:\tretw.n

42000100 <leaf>:
42000100:\tentry\ta1, 48
42000103:\tretw.n

42000200 <dispatch>:
42000200:\tentry\ta1, 64
42000203:\tl32r\ta8, 41ffff0c <_lit+0xc> (3fc90004 <s_table>)
42000206:\tcallx8\ta8
42000209:\tl32i\ta10, a2, 0
4200020c:\tcallx8\ta10
4200020f:\tretw.n

42000300 <handler>:
42000300:\tentry\ta1, 96
42000303:\tretw.n
"""


def graph(cfg=None):
    g = stack_walk.Graph([])
    g.load(DISASM, {})
    g.link()
    g.apply(cfg or {})
    return g


class StackWalk(unittest.TestCase):
    def test_resolved_long_call_is_an_edge(self):
        g = graph()
        self.assertIn(0x42000100, g.funcs[0x42000000]["calls"])

    def test_data_symbol_and_offset_targets_count_as_unresolved(self):
        g = graph()
        self.assertEqual(g.funcs[0x42000000]["ind"], 2)
        self.assertEqual(g.funcs[0x42000000]["calls"], {0x42000100, 0x42000200})

    def test_indirect_without_table(self):
        self.assertEqual(graph().indirect(0x42000000), (4, 0))

    def test_tabled_function_still_counts_its_indirect_calls(self):
        g = graph({"pointers": {"dispatch": ["handler"]}})
        self.assertIn(0x42000300, g.funcs[0x42000200]["calls"])
        self.assertEqual(g.indirect(0x42000000), (2, 2))

    def test_worst_path_follows_table_edges(self):
        g = graph({"pointers": {"dispatch": ["handler"]}})
        size, path, _ = g.worst(0x42000000, {}, [], {})
        self.assertEqual(size, 32 + 64 + 96)
        self.assertEqual([n for n, _ in path], ["entry_fn", "dispatch", "handler"])


if __name__ == "__main__":
    unittest.main()
