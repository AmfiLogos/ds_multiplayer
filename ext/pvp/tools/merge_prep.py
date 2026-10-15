#!/usr/bin/env python3
"""Prepare the PvP -> src merge in ext/pvp/merge/ (src is only READ). For each file that differs between src and the
PvP copy, the base is the src snapshot (src/old_versions/<stem>-<date>.<ext>, or the current src file) that is closest to
the PvP copy; then git merge-file src base pvp -> merge/tree/<file>. Prints conflicts per file."""
import os, re, subprocess, sys, glob, shutil, filecmp
ROOT = os.path.expanduser('~/ds_multiplayer'); SRC = f'{ROOT}/src'; PVP = f'{ROOT}/ext/pvp/src'; OUT = f'{ROOT}/ext/pvp/merge'
SKIP = re.compile(r'(old_versions|/build/|_bin$|\.dSYM|__pycache__|\.o$|/build$)')
def ndiff(a, b):
    r = subprocess.run(['diff', a, b], capture_output=True, text=True)
    return sum(1 for l in r.stdout.splitlines() if l[:1] in '<>')
def cands(rel):
    base = os.path.basename(rel); stem, ext = os.path.splitext(base)
    c = [f'{SRC}/{rel}']
    fe = f'{ROOT}/ext/foreign_expedition/src/{rel}'       # FE's copy: PvP's copy came from it (FE merged first)
    if os.path.exists(fe): c.append(fe)
    for p in glob.glob(f'{SRC}/old_versions/*'):
        b = os.path.basename(p)
        if b.startswith(stem + '-') or b.startswith(base + '-'):
            c.append(p)
    return c
tree = f'{OUT}/tree'; bases = f'{OUT}/base'
for d in (tree, bases):
    os.makedirs(d, exist_ok=True)
report = []
for dp, dn, fn in os.walk(PVP):
    for f in fn:
        p = os.path.join(dp, f); rel = os.path.relpath(p, PVP)
        if SKIP.search('/' + rel): continue
        s = f'{SRC}/{rel}'
        if not os.path.exists(s):
            report.append(('new', rel, '', 0)); continue
        if filecmp.cmp(s, p, shallow=False): continue
        best = min(cands(rel), key=lambda b: ndiff(b, p))
        bdst = f'{bases}/{rel}'; os.makedirs(os.path.dirname(bdst), exist_ok=True); shutil.copy2(best, bdst)
        dst = f'{tree}/{rel}'; os.makedirs(os.path.dirname(dst), exist_ok=True); shutil.copy2(s, dst)
        r = subprocess.run(['git', 'merge-file', '-L', 'src', '-L', 'base', '-L', 'pvp', dst, bdst, p], capture_output=True, text=True)
        report.append(('merge', rel, os.path.relpath(best, ROOT), r.returncode))
for k, rel, b, rc in sorted(report):
    print(f'{k:5} {rel:34} base={b:50} conflicts={rc}')
