#!/usr/bin/env python3
"""close_x_guard.py - CLOSE RULE guard (owner 2026-10-25: the game's red X on every multiplayer window).

Every window that registers for Escape (`static mpesc_win NAME = { ... }` in src/*.c) must also get the game's red X
(`mpesc_close_x(&NAME, ...)` somewhere in src/), or say in its initializer why it has none (`.nox = "..."`, for example
our notice box, which keeps the game's own buttons). A new window without either fails the build (src/build.sh).

usage: close_x_guard.py [src_dir]     exit 0 = ok, 1 = a window without a red X (listed)
"""
import glob
import os
import re
import sys

src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src')
files = sorted(glob.glob(os.path.join(src, '*.c')))
text = {f: open(f, encoding='utf-8', errors='replace').read() for f in files}
everything = '\n'.join(text.values())
def declarators(t):
    """(name, initializer, offset) of every `static mpesc_win a = {...}, b = {...};` (also several on one statement)"""
    for m in re.finditer(r'\bstatic\s+mpesc_win\s+(?!\*)', t):
        i, depth, start = m.end(), 0, m.end()
        while i < len(t):
            c = t[i]
            if c == '{': depth += 1
            elif c == '}': depth -= 1
            elif depth == 0 and c in ',;':
                d = re.match(r'\s*(\w+)\s*=\s*\{(.*)\}\s*$', t[start:i], re.S)
                if d: yield d.group(1), d.group(2), start
                start = i + 1
                if c == ';': break
            i += 1


bad, seen = [], 0
for f, t in text.items():
    for name, init, off in declarators(t):
        seen += 1
        if re.search(r'\.nox\s*=\s*"[^"]+"', init):
            continue
        if re.search(r'\bmpesc_close_x\s*\(\s*&\s*' + re.escape(name) + r'\b', everything):
            continue
        line = t.count('\n', 0, off) + 1
        bad.append(f'{os.path.relpath(f, src)}:{line}: {name}: no mpesc_close_x(&{name}, ...) and no .nox = "why"')
if bad:
    print('close_x_guard: windows without the game\'s red X (CLOSE RULE, src/mp_esc.h mpesc_close_x):', file=sys.stderr)
    for b in bad:
        print('  ' + b, file=sys.stderr)
    sys.exit(1)
print(f'close_x_guard: ok ({seen} windows)')
