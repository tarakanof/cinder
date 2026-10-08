#!/usr/bin/env python3
import collections
import glob
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FUNC = re.compile(r'^([0-9a-f]{8}) <([^>]+)>:$')
ADDR = re.compile(r'^\s*([0-9a-f]{8}):')
ENTRY = re.compile(r'\sentry\s+a1, (0x[0-9a-f]+|\d+)')
CALL = re.compile(r'\scall(?:0|8)\s+([0-9a-f]{8}) <([^>+]+)(\+0x[0-9a-f]+)?>')
L32R = re.compile(r'\sl32r\s+(a\d+), [0-9a-f]+ <[^>]*> \(([0-9a-f]+) <([^>+]+)(\+0x[0-9a-f]+)?>\)')
CALLX = re.compile(r'\scallx(?:0|8)\s+(a\d+)')
WRITES = re.compile(r'^\s*[0-9a-f]+:\s+([a-z0-9_.]+)\s+(a\d+)\b')
KEEPS = ('s', 'b', 'j', 'call', 'ret', 'l32r', 'entry', 'wsr', 'wur', 'ee.')


def tool(name):
    return os.environ.get('XTENSA_PREFIX', 'xtensa-esp32s3-elf-') + name


def run(*args):
    return subprocess.run(args, capture_output=True, text=True, check=True).stdout


def rom_elf():
    hits = sorted(glob.glob(os.path.expanduser('~/.espressif/tools/esp-rom-elfs/*/esp32s3_rev0_rom.elf')))
    return hits[-1] if hits else None


class Graph:
    def __init__(self, elfs):
        self.funcs, self.by_name, self.src = {}, collections.defaultdict(list), {}
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
            self.load(run(tool('objdump'), '-d', '--no-show-raw-insn', elf), sizes)

    def load(self, text, sizes):
        cur, regs, end = None, {}, 0
        for line in text.splitlines():
            m = FUNC.match(line.strip())
            if m:
                a = int(m.group(1), 16)
                cur = {'name': m.group(2), 'frame': None, 'calls': set(), 'ind': 0}
                self.funcs[a] = cur
                self.by_name[cur['name']].append(a)
                regs, end = {}, a + sizes.get(a, 0) if sizes.get(a) else 0
                continue
            am = ADDR.match(line)
            if cur is None or (am and end and int(am.group(1), 16) >= end):
                continue
            if cur['frame'] is None:
                e = ENTRY.search(line)
                if e:
                    cur['frame'] = int(e.group(1), 0)
                    continue
            c = CALL.search(line)
            if c:
                if not c.group(3):
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
                    cur['calls'].add(t[0])
                else:
                    cur['ind'] += 1
                continue
            w = WRITES.match(line)
            if w and not w.group(1).startswith(KEEPS):
                regs.pop(w.group(2), None)

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
        for key, targets in cfg.get('pointers', {}).items():
            callers = self.names(key[3:]) if key.startswith('re:') else [key]
            dst = {x for t in targets for x in self.addrs(t)}
            for n in callers:
                for x in self.addrs(n):
                    self.funcs[x]['calls'] |= dst

    def worst(self, a, memo, stack):
        if a in memo:
            return memo[a]
        f = self.funcs.get(a)
        if f is None or a in stack:
            return 0, []
        stack.append(a)
        best = (0, [])
        for c in f['calls']:
            w = self.worst(c, memo, stack)
            if w[0] > best[0]:
                best = w
        stack.pop()
        frame = f['frame'] or 0
        memo[a] = (frame + best[0], [(f['name'], frame)] + best[1])
        return memo[a]

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
    print('%-10s %6s %6s %6s %6s' % ('task', 'path', 'recur', 'total', 'stack'))
    for t in tasks:
        spec = cfg['tasks'][t]
        path = g.worst(g.root(spec['entry'], spec.get('file')), {}, [])
        recur = spec.get('recursion', 0)
        total = path[0] + recur + overhead
        print('%-10s %6d %6d %6d %6s' % (t, path[0], recur, total, spec.get('stack', '')))
        if '--path' in sys.argv:
            acc = 0
            for n, fr in path[1]:
                acc += fr
                print('    %6d %5d %s' % (acc, fr, n))


if __name__ == '__main__':
    main()
