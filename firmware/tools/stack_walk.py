#!/usr/bin/env python3
import bisect
import collections
import concurrent.futures
import glob
import json
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FUNC = re.compile(r'^([0-9a-f]{8}) <([^>]+)>:$')
ENTRY = re.compile(r'\sentry\s+a1, (0x[0-9a-f]+|\d+)')
CALL = re.compile(r'\scall(?:0|8)\s+([0-9a-f]{8}) <([^>+]+)(\+0x[0-9a-f]+)?>')
L32R = re.compile(r'\sl32r\s+(a\d+), [0-9a-f]+ <[^>]*> \(([0-9a-f]+) <([^>+]+)(\+0x[0-9a-f]+)?>\)')
CALL_MID = re.compile(r'\scall(?:0|8)\s+[0-9a-f]{8} <[^>+]+\+0x')
CALLS = re.compile(r'\scallx?(?:0|8)\s')
INSN = re.compile(r'^\s*([0-9a-f]{8}):\s+(\S+)\s*(.*)$')
TARGET = re.compile(r'([0-9a-f]{8}) <[^>]*>$')
JUMPS = ('j', 'loop', 'loopnez', 'loopgtz')
STOPS = ('j', 'jx', 'ret', 'ret.n', 'retw', 'retw.n', 'rfe', 'rfi', 'rfde', 'rfwo', 'rfwu', 'ill', 'ill.n', '.byte')
CALLX = re.compile(r'\scallx(?:0|8)\s+(a\d+)')
WRITES = re.compile(r'^\s*[0-9a-f]+:\s+([a-z0-9_.]+)\s+(a\d+)\b')
KEEPS = ('s8i', 's16i', 's32i', 's32e', 'ssi', 'ssx', 'sdi', 'b', 'j', 'call', 'ret', 'entry', 'wsr', 'wur')
INF = float('inf')
POOL = concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4)
SECTIONS = {}


def tool(name):
    return os.environ.get('XTENSA_PREFIX', 'xtensa-esp32s3-elf-') + name


def run(*args):
    return subprocess.run(args, capture_output=True, text=True, check=True).stdout


def rom_elf():
    hits = sorted(glob.glob(os.path.expanduser('~/.espressif/tools/esp-rom-elfs/*/esp32s3_rev0_rom.elf')))
    return hits[-1] if hits else None


def words(elf, addr, n):
    if elf not in SECTIONS:
        with open(elf, 'rb') as fh:
            data = fh.read()
        shoff, = struct.unpack_from('<I', data, 0x20)
        size, count = struct.unpack_from('<HH', data, 0x2e)
        secs = []
        for i in range(count):
            _, typ, flags, vaddr, off, n_bytes = struct.unpack_from('<6I', data, shoff + i * size)
            if typ == 1 and flags & 2:
                secs.append((vaddr, off, n_bytes))
        SECTIONS[elf] = data, secs
    data, secs = SECTIONS[elf]
    for vaddr, off, n_bytes in secs:
        if vaddr <= addr < vaddr + n_bytes:
            return struct.unpack_from('<%dI' % min(n, (vaddr + n_bytes - addr) // 4), data, off + addr - vaddr)
    return ()


def sweep(lines):
    out = []
    for line in lines:
        m = INSN.match(line)
        if m:
            out.append((int(m.group(1), 16), line, m.group(2), m.group(3)))
    return out


def merge(code, seq, stop, end):
    for i, (a, line, mn, ops) in enumerate(seq):
        if a in code:
            return
        if i + 1 < len(seq):
            n = seq[i + 1][0] - a
        elif stop == end:
            n = end - a
        else:
            return
        code[a] = (line, mn, ops, n)


def table(elf, code, pred, a, s, e):
    for _ in range(8):
        a = pred.get(a)
        if a is None:
            return None, None
        lit = L32R.search(code[a][0])
        if lit:
            v, got = int(lit.group(2), 16), []
            for w in words(elf, v, 1024) if elf else ():
                if not s <= w < e:
                    break
                got.append(w)
            return got or None, None if got or lit.group(4) or s <= v < e else v
    return None, None


def descend(f, elf):
    s, e, code = f['start'], f['end'], f['code']
    live, pred, missing, whole, tails, todo = set(), {}, set(), False, [], [s]
    while todo:
        a = todo.pop()
        while s <= a < e and a not in live:
            if a not in code:
                missing.add(a)
                break
            line, mn, ops, n = code[a]
            live.add(a)
            t = TARGET.search(ops)
            if t and (mn in JUMPS or (mn.startswith('b') and not mn.startswith('break'))):
                todo.append(int(t.group(1), 16))
            if mn == 'jx':
                tab, tail = table(elf, code, pred, a, s, e)
                whole |= tab is None and tail is None
                todo += tab or []
                tails += [] if tail is None else [tail]
            if mn in STOPS:
                break
            pred[a + n] = a
            a += n
    return live, missing, whole, tails


class Graph:
    def __init__(self, elfs):
        self.funcs, self.by_name, self.src = {}, collections.defaultdict(list), {}
        self.stats, self.spans = collections.Counter(), {}
        for elf in elfs:
            sizes = {}
            for line in run(tool('nm'), '-S', '--defined-only', elf).splitlines():
                p = line.split()
                if len(p) == 4 and p[2] in 'tTwW':
                    a = int(p[0], 16)
                    sizes[a] = max(sizes.get(a, 0), int(p[1], 16))
            for line in run(tool('nm'), '-l', '--defined-only', elf).splitlines():
                p = line.split(None, 3)
                if len(p) == 4 and p[1] in 'tTwW':
                    self.src[int(p[0], 16)] = p[3]
            self.load(run(tool('objdump'), '-d', '--no-show-raw-insn', elf), sizes, elf)
        self.link()

    def load(self, text, sizes, elf=None):
        self.stats['mid_sweep'] += len(CALL_MID.findall(text))
        spans = self.spans.setdefault(elf, [])
        fns = self.decode(text, sizes, elf)
        while fns:
            spans += [(f['start'], f['end']) for f in fns]
            spans.sort()
            gaps = []
            for t in sorted({t for f in fns for t in self.funcs[f['start']]['mids']}):
                i = bisect.bisect_right(spans, (t, INF)) - 1
                if elf and t not in self.funcs and (i < 0 or t >= spans[i][1]) and i + 1 < len(spans):
                    gaps.append(t)
            fns = []
            for k, t in enumerate(gaps):
                stop = min([spans[bisect.bisect_right(spans, (t, INF))][0]] + gaps[k + 1:k + 2])
                fns += self.decode(run(tool('objdump'), '-d', '--no-show-raw-insn', '--start-address=0x%x' % t,
                                       '--stop-address=0x%x' % stop, elf), {t: stop - t}, elf)

    def decode(self, text, sizes, elf):
        heads = []
        for line in text.splitlines():
            m = FUNC.match(line.strip())
            if m:
                heads.append((int(m.group(1), 16), m.group(2), []))
            elif heads and INSN.match(line):
                heads[-1][2].append(line)
        fns = []
        for k, (a, name, lines) in enumerate(heads):
            nxt = heads[k + 1][0] if k + 1 < len(heads) and heads[k + 1][0] > a else None
            end = a + sizes[a] if sizes.get(a) else nxt
            seq = [x for x in sweep(lines) if end is None or x[0] < end]
            if end is None:
                end = seq[-1][0] + 4 if seq else a + 1
            code = {}
            for i, (b, line, mn, ops) in enumerate(seq):
                code[b] = (line, mn, ops, (seq[i + 1][0] if i + 1 < len(seq) else end) - b)
            fns.append({'start': a, 'end': end, 'name': name, 'code': code, 'tried': set()})
        todo = fns
        while todo:
            need = []
            for f in todo:
                f['live'], f['missing'], f['whole'], f['tails'] = descend(f, elf)
                need += [(f, t) for t in sorted(f['missing'] - f['tried'])]
            if not elf:
                break
            outs = list(POOL.map(lambda ft: run(tool('objdump'), '-d', '--no-show-raw-insn',
                                                 '--start-address=0x%x' % ft[1],
                                                 '--stop-address=0x%x' % min(ft[0]['end'], ft[1] + 256), elf), need))
            for (f, t), out in zip(need, outs):
                f['tried'].add(t)
                merge(f['code'], sweep(out.splitlines()), min(f['end'], t + 256), f['end'])
                self.stats['resynced'] += 1
            todo = list({id(f): f for f, _ in need}.values())
        for f in fns:
            self.scan(f)
        return fns

    def scan(self, f):
        cur = {'name': f['name'], 'frame': None, 'calls': set(), 'xcalls': list(f['tails']), 'ind': 0, 'mids': []}
        self.funcs[f['start']] = cur
        self.by_name[cur['name']].append(f['start'])
        code, live = f['code'], set(f['code']) if f['whole'] else f['live']
        self.stats['whole'] += f['whole']
        self.stats['missing'] += len(f['missing'] - set(code))
        for a in code:
            if a not in live and CALLS.search(code[a][0]):
                c = CALL.search(code[a][0])
                dead = 'dead_start' if c and not c.group(3) else 'dropped'
                self.stats[dead] += 1
                cur['ind'] += dead == 'dead_start'
        regs, last = {}, None
        for a in sorted(live):
            line, n = code[a][0], code[a][3]
            if last != a:
                regs = {}
            last = a + n
            if cur['frame'] is None:
                e = ENTRY.search(line)
                if e:
                    cur['frame'] = int(e.group(1), 0)
                    continue
            c = CALL.search(line)
            if c:
                if c.group(3):
                    cur['mids'].append(int(c.group(1), 16))
                else:
                    cur['calls'].add(int(c.group(1), 16))
                continue
            lit = L32R.search(line)
            if lit:
                regs[lit.group(1)] = (int(lit.group(2), 16), lit.group(4))
                continue
            x = CALLX.search(line)
            if x:
                t = regs.get(x.group(1))
                if t and not t[1]:
                    cur['xcalls'].append(t[0])
                else:
                    cur['ind'] += 1
                continue
            w = WRITES.match(line)
            if w and not w.group(1).startswith(KEEPS):
                regs.pop(w.group(2), None)

    def link(self):
        for f in self.funcs.values():
            for t in f['xcalls']:
                if t in self.funcs:
                    f['calls'].add(t)
                else:
                    f['ind'] += 1
            for t in f['mids']:
                if t in self.funcs:
                    f['calls'].add(t)
                    self.stats['mid_anon'] += 1
                else:
                    f['ind'] += 1
                    self.stats['mid_live'] += 1
            f['xcalls'], f['mids'] = [], []

    def names(self, pattern):
        rx = re.compile(pattern)
        return [n for n in self.by_name if rx.search(n)]

    def addrs(self, name):
        return self.by_name.get(name, [])

    def apply(self, cfg):
        leaves = set(cfg.get('leaves', []))
        for f in self.funcs.values():
            f['calls'] = {c for c in f['calls'] if c in self.funcs and self.funcs[c]['name'] not in leaves}
        for a_pat, b_pat in cfg.get('infeasible', []):
            drop = {x for n in self.names(b_pat) for x in self.addrs(n)}
            for n in self.names(a_pat):
                for x in self.addrs(n):
                    self.funcs[x]['calls'] -= drop
        nesting = cfg.get('nesting', {})
        self.group = {x for n in nesting.get('group', []) for x in self.addrs(n)}
        self.group_depth = nesting.get('depth', 0)
        self.tabled = set()
        for key, targets in cfg.get('pointers', {}).items():
            callers = self.names(key[3:]) if key.startswith('re:') else [key]
            dst = {x for t in targets for x in self.addrs(t)}
            for n in callers:
                for x in self.addrs(n):
                    self.funcs[x]['calls'] |= dst
                    self.tabled.add(x)

    def indirect(self, a):
        reach = self.reach(a)
        tabled = sum(self.funcs[x]['ind'] for x in reach if x in self.tabled)
        return sum(self.funcs[x]['ind'] for x in reach) - tabled, tabled

    def worst(self, a, memo, stack, onstack, nest=0):
        key = (a, nest)
        hit = memo.get(key)
        if hit is not None and not any(x in onstack for x in hit[3]):
            return hit
        f = self.funcs.get(a)
        if f is None:
            return 0, [], INF, ()
        if a in onstack:
            return 0, [], onstack[a], ()
        if a in self.group:
            if nest >= self.group_depth:
                return 0, [], INF, ()
            nest += 1
        depth = len(stack)
        onstack[a] = depth
        stack.append(a)
        best, low = (0, [], INF, ()), INF
        for c in f['calls']:
            w = self.worst(c, memo, stack, onstack, nest)
            low = min(low, w[2])
            if w[0] > best[0]:
                best = w
        stack.pop()
        del onstack[a]
        frame = f['frame'] or 0
        r = (frame + best[0], [(f['name'], frame)] + best[1], low if low < depth else INF, (a,) + best[3])
        if low >= depth:
            memo[key] = r
        return r

    def reach(self, a):
        seen, todo = set(), [a]
        while todo:
            x = todo.pop()
            if x in seen or x not in self.funcs:
                continue
            seen.add(x)
            todo.extend(self.funcs[x]['calls'])
        return seen

    def through(self, a, target, head, memo, stack, onstack):
        if a == target:
            return head[0], head[1], INF, (a,)
        hit = memo.get(a)
        if hit is not None and not any(x in onstack for x in hit[3]):
            return hit
        if a not in self.funcs:
            return None, [], INF, ()
        if a in onstack:
            return None, [], onstack[a], ()
        depth = len(stack)
        onstack[a] = depth
        stack.append(a)
        best, low = (None, [], INF, ()), INF
        for c in self.funcs[a]['calls']:
            w = self.through(c, target, head, memo, stack, onstack)
            low = min(low, w[2])
            if w[0] is not None and (best[0] is None or w[0] > best[0]):
                best = w
        stack.pop()
        del onstack[a]
        frame = self.funcs[a]['frame'] or 0
        if best[0] is None:
            r = (None, [], low if low < depth else INF, ())
        else:
            r = (frame + best[0], [(self.funcs[a]['name'], frame)] + best[1], low if low < depth else INF,
                 (a,) + best[3])
        if low >= depth:
            memo[a] = r
        return r

    def root(self, name, file_suffix=None):
        cands = [a for a in self.addrs(name) if not file_suffix or file_suffix in self.src.get(a, '')]
        if len(cands) != 1:
            raise SystemExit('root %s (%s): %d matches' % (name, file_suffix, len(cands)))
        return cands[0]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if not args:
        raise SystemExit('usage: stack_walk.py build/cinder.elf [task ...] [--path]')
    cfg = json.load(open(os.path.join(HERE, 'stack_walk.json')))
    elfs = [args[0]] + ([rom_elf()] if rom_elf() else [])
    g = Graph(elfs)
    g.apply(cfg)
    overhead = cfg['overhead']
    tasks = args[1:] or list(cfg['tasks'])
    print('estimates, not proven bounds: indirect calls not resolved statically count as 0 B')
    print('unresolved: no pointer-table entry; tabled: in a function with one, assumed covered by its listed targets')
    print('LVGL event nesting (%s) is a heuristic cut at %d entries per path, not a bound: a path never repeats '
          'a function, so a nested dispatch re-running the same handlers is not counted'
          % (', '.join(cfg.get('nesting', {}).get('group', [])), cfg.get('nesting', {}).get('depth', 0)))
    st = g.stats
    print('objdump resync: %d branch targets re-disassembled, %d still undecoded; %d functions with an unresolved jx '
          'kept whole' % (st['resynced'], st['missing'], st['whole']))
    print('call sites only in unreachable bytes: %d to a symbol start (counted as unresolved: EH landing pads, '
          'unreferenced code past a symbol), %d to <sym+0x..> or through a register (dropped as decode garbage)'
          % (st['dead_start'], st['dropped']))
    print('call0/call8 to <sym+0x..>: %d in the linear sweep; in reachable code %d past the symbol\'s size '
          '(decoded as anonymous functions), %d inside a function (counted as unresolved)'
          % (st['mid_sweep'], st['mid_anon'], st['mid_live']))
    print('%-10s %6s %6s %8s %6s %6s %10s %6s' % ('task', 'path', 'recur', 'estimate', 'stack', 'margin', 'unresolved',
                                                 'tabled'))
    for t in tasks:
        spec = cfg['tasks'][t]
        entry = g.root(spec['entry'], spec.get('file'))
        path = g.worst(entry, {}, [], {})
        best, recur, head = path[0], 0, None
        for r in cfg.get('recursion', []):
            for h in g.addrs(r['head']):
                extra = (r['depth'] - 1) * sum(g.funcs[x]['frame'] or 0 for n in r['cycle'] for x in g.addrs(n))
                w = g.through(entry, h, g.worst(h, {}, [], {}), {}, [], {})
                if w[0] is not None and w[0] + extra > best + recur:
                    best, recur, head, path = w[0], extra, r, w
        unresolved, tabled = g.indirect(entry)
        total = best + recur + overhead
        stack = spec.get('stack')
        margin = stack - total if stack else ''
        print('%-10s %6d %6d %8d %6s %6s %10d %6d' % (t, best, recur, total, stack or '', margin, unresolved, tabled))
        if '--path' in sys.argv:
            acc = 0
            for n, fr in path[1]:
                acc += fr
                print('    %6d %5d %s' % (acc, fr, n))
                if head and n == head['head']:
                    print('    %6s %5d recursion: %s x %d more' % ('', recur, '/'.join(head['cycle']), head['depth'] - 1))


if __name__ == '__main__':
    main()
