#!/usr/bin/env python3
"""Inclusive main-thread samples per dsrig function (inlined frames resolved with atos -i). sym_tree.py <sample.txt> <dylib>"""
import re, subprocess, sys, collections
txt, dylib = sys.argv[1], sys.argv[2]
lines = open(txt).read().split('\n')
start = next(i for i, l in enumerate(lines) if 'com.apple.main-thread' in l or 'Main Thread' in l)
total = int(re.search(r'(\d+) Thread', lines[start]).group(1))
nodes = []   # (depth, count, addr or None)
load = None
for l in lines[start + 1:]:
    if not l.startswith('    +') and not l.startswith('    |'):
        if l.strip() == '' or re.match(r'\s+\d+ Thread', l): break
    m = re.match(r'^(\s*[+!:| ]*)(\d+) (.*)', l)
    if not m: continue
    depth = len(m.group(1)); cnt = int(m.group(2)); rest = m.group(3)
    a = re.search(r'(\S+)  \(in dsrig\.dylib\) \+ (\d+)  \[(0x[0-9a-f]+)\]', rest)
    nodes.append((depth, cnt, int(a.group(3), 16) if a else None, rest[:60]))
for l in lines:
    m = re.match(r'\s*(0x[0-9a-f]+) -\s+0x[0-9a-f]+ \+?dsrig\.dylib', l)
    if m: load = m.group(1); break
addrs = sorted({hex(n[2]) for n in nodes if n[2]})
out = subprocess.run(['atos', '-i', '-o', dylib, '-l', load] + addrs, capture_output=True, text=True).stdout
# atos -i prints the inline chain (innermost first) per address, blank line between addresses
chains = {}; blocks = out.strip().split('\n\n')
for a, b in zip(addrs, blocks):
    chains[int(a, 16)] = [re.sub(r' \(in dsrig.dylib\).*', '', x).strip() for x in b.split('\n') if x.strip()]
incl = collections.Counter(); stack = []
for depth, cnt, addr, rest in nodes:
    while stack and stack[-1][0] >= depth: stack.pop()
    fn = set(chains.get(addr, [])) if addr else set()
    above = set().union(*[s[1] for s in stack]) if stack else set()
    for f in fn - above: incl[f] += cnt
    stack.append((depth, fn))
print(f'main thread samples: {total}')
for f, c in incl.most_common(int(sys.argv[3]) if len(sys.argv) > 3 else 45): print(f'{c:6d} {100*c/total:5.1f}%  {f}')
