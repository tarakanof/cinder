import os
import random
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
4200000f:\tl32r\ta9, 41ffff08 <_lit+0x8> (42000104 <leaf+0x4>)
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
        size, path = g.worst(0x42000000, {}, [], {})[:2]
        self.assertEqual(size, 32 + 64 + 96)
        self.assertEqual([n for n, _ in path], ["entry_fn", "dispatch", "handler"])

    def test_long_call_into_a_later_elf_resolves(self):
        g = stack_walk.Graph([])
        g.load("""
42000000 <app_fn>:
42000000:\tentry\ta1, 32
42000003:\tl32r\ta8, 41ffff00 <_lit> (40001000 <rom_fn>)
42000006:\tcallx8\ta8
""", {})
        g.load("""
40001000 <rom_fn>:
40001000:\tentry\ta1, 80
""", {})
        g.link()
        g.apply({})
        self.assertEqual(g.funcs[0x42000000]["calls"], {0x40001000})
        self.assertEqual(g.funcs[0x42000000]["ind"], 0)
        self.assertEqual(g.worst(0x42000000, {}, [], {})[0], 32 + 80)

    def test_init_links_after_every_elf(self):
        texts = iter(["""
42000000 <app_fn>:
42000000:\tentry\ta1, 32
42000003:\tl32r\ta8, 41ffff00 <_lit> (40001000 <rom_fn>)
42000006:\tcallx8\ta8
""", """
40001000 <rom_fn>:
40001000:\tentry\ta1, 80
"""])
        real = stack_walk.run
        stack_walk.run = lambda *a: next(texts) if a[0].endswith("objdump") else ""
        try:
            g = stack_walk.Graph(["app.elf", "rom.elf"])
        finally:
            stack_walk.run = real
        self.assertEqual(g.funcs[0x42000000]["calls"], {0x40001000})
        self.assertEqual(g.funcs[0x42000000]["ind"], 0)

    def test_memo_hit_never_repeats_an_on_path_function(self):
        g = stack_walk.Graph([])
        g.load("""
42000000 <top>:
42000000:\tentry\ta1, 16
42000003:\tcall8\t42000100 <c>
42000006:\tcall8\t42000200 <log>

42000100 <c>:
42000100:\tentry\ta1, 32
42000103:\tcall8\t42000200 <log>

42000200 <log>:
42000200:\tentry\ta1, 64
42000203:\tcall8\t42000100 <c>
""", {})
        g.link()
        g.apply({})
        size, path = g.worst(0x42000000, {}, [], {})[:2]
        names = [n for n, _ in path]
        self.assertEqual(len(names), len(set(names)), names)
        self.assertEqual(size, 16 + 32 + 64)


DESYNC = """
42000000 <fn>:
42000000:\tentry\ta1, 32
42000003:\tbnez.n\ta2, 4200000c <fn+0xc>
42000005:\tcall8\t42000100 <leaf>
42000008:\tj\t42000012 <fn+0x12>
4200000b:\t%s
4200000e:\t.byte\t0xa0
4200000f:\tl32i.n\ta8, a1, 0
42000011:\t.byte\t0x00
42000012:\tretw.n

42000100 <leaf>:
42000100:\tentry\ta1, 48
42000103:\tretw.n

42000200 <big>:
42000200:\tentry\ta1, 400
42000203:\tretw.n
"""

RESYNC = """
4200000c <fn+0xc>:
4200000c:\tcall8\t42000200 <big>
4200000f:\tl32i.n\ta8, a1, 0
"""


def fake_objdump(by_start):
    def run(*args):
        start = [a for a in args if a.startswith("--start-address=")]
        return by_start[int(start[0].split("=")[1], 16)] if start else ""
    return run


def load(text, sizes=None, outputs=None, elf="app.elf", tables=None):
    g = stack_walk.Graph([])
    real = stack_walk.run, stack_walk.words
    stack_walk.run = fake_objdump(outputs or {})
    stack_walk.words = lambda elf, addr, n: (tables or {}).get(addr, ())[:n]
    try:
        g.load(text, sizes or {}, elf)
    finally:
        stack_walk.run, stack_walk.words = real
    g.link()
    g.apply({})
    return g


class Desync(unittest.TestCase):
    GARBAGE = ("quou\ta0, a0, a0", "call8\t42000104 <leaf+0x4>", "l32r\ta5, 41ffff00 <_lit> (42000300 <ghost>)")

    def test_branch_target_inside_a_garbage_instruction_is_re_disassembled(self):
        for garbage in self.GARBAGE:
            g = load(DESYNC % garbage, outputs={0x4200000c: RESYNC})
            self.assertEqual(g.funcs[0x42000000]["calls"], {0x42000100, 0x42000200}, garbage)
            self.assertEqual(g.funcs[0x42000000]["ind"], 0, garbage)
            self.assertEqual(g.worst(0x42000000, {}, [], {})[0], 32 + 400, garbage)
            self.assertEqual(g.stats["resynced"], 1)
            self.assertEqual(g.stats["mid_live"], 0)

    def test_call_decoded_only_in_dead_bytes_is_dropped_and_counted(self):
        g = load(DESYNC % "call8\t42000104 <leaf+0x4>", outputs={0x4200000c: RESYNC})
        self.assertEqual(g.stats["mid_sweep"], 1)
        self.assertEqual(g.stats["dropped"], 1)

    def test_unreachable_call_to_a_symbol_start_counts_as_unresolved(self):
        text = """
42000000 <fn>:
42000000:\tentry\ta1, 32
42000003:\tcall8\t42000100 <leaf>
42000006:\tretw.n
42000008:\tcall8\t42000200 <big>
4200000b:\tretw.n

42000100 <leaf>:
42000100:\tentry\ta1, 48
42000103:\tretw.n

42000200 <big>:
42000200:\tentry\ta1, 400
42000203:\tretw.n
"""
        g = load(text)
        self.assertEqual(g.funcs[0x42000000]["calls"], {0x42000100})
        self.assertEqual(g.funcs[0x42000000]["ind"], 1)
        self.assertEqual((g.stats["dead_start"], g.stats["dropped"]), (1, 0))

    def test_without_an_elf_the_swallowed_call_is_reported_not_guessed(self):
        g = load(DESYNC % "quou\ta0, a0, a0", elf=None)
        self.assertEqual(g.funcs[0x42000000]["calls"], {0x42000100})
        self.assertEqual(g.stats["missing"], 1)

    def test_jump_table_targets_are_followed(self):
        text = """
42000000 <sw>:
42000000:\tentry\ta1, 32
42000003:\tmovi.n\ta8, 1
42000005:\tbgeu\ta8, a2, 4200000c <sw+0xc>
42000008:\tretw.n
4200000a:\t.byte\t0x00
4200000b:\t.byte\t0x00
4200000c:\tl32r\ta8, 41ffff00 <_lit> (3c000000 <tbl>)
4200000f:\tslli\ta3, a2, 2
42000012:\tadd.n\ta3, a8, a3
42000014:\tl32i.n\ta3, a3, 0
42000016:\tjx\ta3
42000019:\tcall8\t42000100 <leaf>
4200001c:\tretw.n
4200001e:\tcall8\t42000200 <big>
42000021:\tretw.n

42000100 <leaf>:
42000100:\tentry\ta1, 48
42000103:\tretw.n

42000200 <big>:
42000200:\tentry\ta1, 400
42000203:\tretw.n
"""
        g = load(text, tables={0x3c000000: (0x42000019, 0x4200001e, 0x3c001000)})
        self.assertEqual(g.funcs[0x42000000]["calls"], {0x42000100, 0x42000200})
        self.assertEqual(g.stats["whole"], 0)

    GUARD = "bgeui\ta2, 1, 42000014 <sw+0x14>"
    SWITCH = """
42000000 <sw>:
42000000:\tentry\ta1, 32
42000003:\t%s
42000006:\tl32r\ta8, 41ffff00 <_lit> (3c000000 <tbl>)
42000009:\taddx4\ta8, a2, a8
4200000c:\tl32i.n\ta8, a8, 0
4200000e:\tl32r\ta9, 41ffff04 <_lit+0x4> (3fc90000 <s_data>)
42000011:\t%s
42000014:\tretw.n
42000016:\tcall8\t42000200 <big>
42000019:\tretw.n

42000200 <big>:
42000200:\tentry\ta1, 400
42000203:\tretw.n
"""

    def test_jx_table_comes_from_the_jx_register_not_the_nearest_literal(self):
        g = load(self.SWITCH % (self.GUARD, "jx\ta8"), tables={0x3c000000: (0x42000016,)})
        self.assertEqual(g.funcs[0x42000000]["calls"], {0x42000200})
        self.assertGreaterEqual(g.worst(0x42000000, {}, [], {})[0], 432)
        self.assertEqual((g.stats["whole"], g.stats["dropped"]), (0, 0))

    def test_jx_through_an_unrelated_data_literal_keeps_the_whole_function(self):
        g = load(self.SWITCH % (self.GUARD, "jx\ta9"), tables={0x3c000000: (0x42000016,)})
        self.assertGreaterEqual(g.worst(0x42000000, {}, [], {})[0], 432)
        self.assertEqual(g.stats["whole"], 1)

    def test_jump_table_without_a_bound_check_is_not_resolved(self):
        g = load(self.SWITCH % ("nop", "jx\ta8"), tables={0x3c000000: (0x42000014,) * 1025 + (0x42000016,)})
        self.assertGreaterEqual(g.worst(0x42000000, {}, [], {})[0], 432)
        self.assertEqual(g.stats["whole"], 1)

    def test_jump_table_with_an_entry_outside_the_function_is_not_resolved(self):
        g = load(self.SWITCH % ("bgeui\ta2, 2, 42000014 <sw+0x14>", "jx\ta8"),
                 tables={0x3c000000: (0x42000014, 0x3c001000)})
        self.assertGreaterEqual(g.worst(0x42000000, {}, [], {})[0], 432)
        self.assertEqual(g.stats["whole"], 1)

    def test_loop_end_target_is_followed_and_resynced(self):
        text = """
42000000 <fn>:
42000000:\tentry\ta1, 32
42000003:\tloopnez\ta3, 4200000c <fn+0xc>
42000006:\tcall8\t42000100 <leaf>
42000009:\tj\t42000006 <fn+0x6>
4200000b:\tquou\ta0, a0, a0
4200000e:\t.byte\t0xa0
4200000f:\tretw.n

42000100 <leaf>:
42000100:\tentry\ta1, 48
42000103:\tretw.n

42000200 <big>:
42000200:\tentry\ta1, 400
42000203:\tretw.n
"""
        g = load(text, outputs={0x4200000c: RESYNC.replace("l32i.n\ta8, a1, 0", "retw.n")})
        self.assertEqual(g.funcs[0x42000000]["calls"], {0x42000100, 0x42000200})

    def test_unreachable_long_call_to_a_function_counts_as_unresolved(self):
        text = """
42000000 <fn>:
42000000:\tentry\ta1, 32
42000003:\tretw.n
42000005:\tl32r\ta8, 41ffff00 <_lit> (42000200 <big>)
42000008:\tcallx8\ta8
4200000b:\tretw.n

42000200 <big>:
42000200:\tentry\ta1, 400
42000203:\tretw.n
"""
        g = load(text)
        self.assertEqual(g.funcs[0x42000000]["ind"], 1)
        self.assertEqual((g.stats["dead_start"], g.stats["dropped"]), (1, 0))

    def test_reached_literal_before_an_unreached_long_call_counts_as_unresolved(self):
        text = """
42000000 <fn>:
42000000:\tentry\ta1, 32
42000003:\tl32r\ta8, 41ffff00 <_lit> (42000200 <big>)
42000006:\tj\t4200000c <fn+0xc>
42000009:\tcallx8\ta8
4200000c:\tretw.n

42000200 <big>:
42000200:\tentry\ta1, 400
42000203:\tretw.n
"""
        g = load(text)
        self.assertEqual(g.funcs[0x42000000]["ind"], 1)
        self.assertEqual((g.stats["dead_start"], g.stats["dropped"]), (1, 0))

    def test_undecoded_and_stray_branch_targets_count_on_the_owner(self):
        text = """
42000000 <fn>:
42000000:\tentry\ta1, 32
42000003:\tbnez.n\ta2, 4200000c <fn+0xc>
42000005:\tbeqz.n\ta3, 42000102 <leaf+0x2>
42000007:\tj\t42000100 <leaf>
4200000b:\tquou\ta0, a0, a0
4200000e:\tretw.n

42000100 <leaf>:
42000100:\tentry\ta1, 48
42000103:\tretw.n
"""
        g = load(text, elf=None)
        self.assertEqual(g.funcs[0x42000000]["ind"], 1)
        self.assertEqual(g.funcs[0x42000000]["calls"], {0x42000100})
        g = load(text.replace("42000102 <leaf+0x2>", "42000302 <leaf+0x202>"), elf=None)
        self.assertEqual(g.funcs[0x42000000]["ind"], 2)

    def test_sibling_stripped_statics_in_a_gap_are_their_own_functions(self):
        text = """
42000000 <fn>:
42000000:\tentry\ta1, 32
42000003:\tcall8\t42000110 <leaf+0x10>
42000006:\tl32r\ta9, 41ffff00 <_lit> (42000116 <leaf+0x16>)
42000009:\tcallx8\ta9
4200000c:\tretw.n

42000100 <leaf>:
42000100:\tentry\ta1, 48
42000103:\tretw.n

42000200 <big>:
42000200:\tentry\ta1, 400
42000203:\tretw.n
"""
        gap = """
42000110 <leaf+0x10>:
42000110:\tentry\ta1, 80
42000113:\tretw.n
42000115:\t.byte\t0x00
42000116:\tentry\ta1, 96
42000119:\tl32r\ta8, 41ffff04 <_lit+0x4> (42000200 <big>)
4200011c:\tcallx8\ta8
4200011f:\tretw.n
"""
        sizes = {0x42000000: 14, 0x42000100: 5, 0x42000200: 5}
        second = "\n42000116 <leaf+0x16>:\n" + gap.split("42000115:\t.byte\t0x00\n")[1]
        g = load(text, sizes=sizes, outputs={0x42000110: gap, 0x42000116: second})
        self.assertEqual(g.funcs[0x42000000]["calls"], {0x42000110, 0x42000116})
        self.assertEqual(g.worst(0x42000000, {}, [], {})[0], 32 + 96 + 400)
        g = load(text.replace("callx8\ta9", "nop.n"), sizes=sizes, outputs={0x42000110: gap})
        self.assertEqual(g.funcs[0x42000116]["name"], "leaf+0x16")
        self.assertEqual(g.funcs[0x42000116]["calls"], {0x42000200})
        self.assertEqual(g.funcs[0x42000110]["ind"], 0)
        self.assertEqual((g.stats["dead_start"], g.stats["mid_live"]), (0, 0))

    def test_unresolved_jx_keeps_the_whole_function(self):
        text = """
42000000 <sw>:
42000000:\tentry\ta1, 32
42000003:\tl32i.n\ta8, a2, 0
42000005:\tjx\ta8
42000008:\tcall8\t42000100 <leaf>
4200000b:\tretw.n

42000100 <leaf>:
42000100:\tentry\ta1, 48
42000103:\tretw.n
"""
        g = load(text)
        self.assertEqual(g.funcs[0x42000000]["calls"], {0x42000100})
        self.assertEqual(g.stats["whole"], 1)

    def test_tail_jump_through_a_literal_is_an_edge(self):
        text = """
42000000 <thunk>:
42000000:\tl32r\ta0, 41ffff00 <_lit> (42000100 <leaf>)
42000003:\tjx\ta0

42000100 <leaf>:
42000100:\tentry\ta1, 48
42000103:\tretw.n
"""
        g = load(text)
        self.assertEqual(g.funcs[0x42000000]["calls"], {0x42000100})
        self.assertEqual(g.stats["whole"], 0)

    def test_call_past_a_symbol_size_decodes_an_anonymous_function(self):
        text = """
42000000 <fn>:
42000000:\tentry\ta1, 32
42000003:\tcall8\t42000110 <leaf+0x10>
42000006:\tcall8\t42000102 <leaf+0x2>
42000009:\tretw.n

42000100 <leaf>:
42000100:\tentry\ta1, 48
42000103:\tretw.n
42000105:\t.byte\t0x00
42000110:\tentry\ta1, 80
42000113:\tretw.n

42000200 <next>:
42000200:\tentry\ta1, 16
42000203:\tretw.n
"""
        anon = """
42000110 <leaf+0x10>:
42000110:\tentry\ta1, 80
42000113:\tretw.n
"""
        g = load(text, sizes={0x42000000: 12, 0x42000100: 5, 0x42000200: 5}, outputs={0x42000110: anon})
        self.assertEqual(g.funcs[0x42000000]["calls"], {0x42000110})
        self.assertEqual(g.funcs[0x42000110]["frame"], 80)
        self.assertEqual(g.funcs[0x42000000]["ind"], 1)
        self.assertEqual((g.stats["mid_anon"], g.stats["mid_live"]), (1, 1))
        self.assertEqual(g.worst(0x42000000, {}, [], {})[0], 32 + 80)


def random_graph(rng, n):
    g = stack_walk.Graph([])
    for a in range(n):
        g.funcs[a] = {"name": "f%d" % a, "frame": rng.choice((16, 32, 48, 64, 96)), "xcalls": [], "ind": 0,
                      "calls": {b for b in range(n) if rng.random() < 0.35}}
        g.by_name["f%d" % a].append(a)
    g.apply({})
    return g


def brute(g, a, seen=()):
    seen = seen + (a,)
    return g.funcs[a]["frame"] + max([brute(g, c, seen) for c in g.funcs[a]["calls"] if c not in seen] or [0])


def brute_through(g, a, target, head, seen=()):
    if a == target:
        return head
    seen = seen + (a,)
    ws = [w for c in g.funcs[a]["calls"] if c not in seen
          for w in [brute_through(g, c, target, head, seen)] if w is not None]
    return g.funcs[a]["frame"] + max(ws) if ws else None


class Exhaustive(unittest.TestCase):
    def test_worst_is_the_longest_simple_path(self):
        rng = random.Random(12)
        for _ in range(400):
            g = random_graph(rng, rng.randint(2, 8))
            size, path = g.worst(0, {}, [], {})[:2]
            names = [n for n, _ in path]
            self.assertEqual(size, brute(g, 0))
            self.assertEqual(len(names), len(set(names)), names)
            self.assertEqual(size, sum(fr for _, fr in path))

    def test_through_is_the_longest_simple_prefix_to_the_head(self):
        rng = random.Random(34)
        for _ in range(400):
            n = rng.randint(2, 8)
            g = random_graph(rng, n)
            h = rng.randrange(1, n)
            head = g.worst(h, {}, [], {})
            w = g.through(0, h, head, {}, [], {})
            self.assertEqual(w[0], brute_through(g, 0, h, head[0]))
            if w[0] is not None:
                self.assertEqual(w[0], sum(fr for _, fr in w[1]))


if __name__ == "__main__":
    unittest.main()
