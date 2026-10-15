#!/usr/bin/env python3
"""Audit merge/full against src: per file, lines the merge REMOVED from src (must be PvP's own intent, else a wrong base)
and lines it ADDED that exist in no src version (genuine PvP / FE lines) vs added lines found in an old src version
(an old src line coming back = suspicious)."""
import os, glob, difflib, sys
R = os.path.expanduser('~/ds_multiplayer'); S = f'{R}/src'; F = f'{R}/ext/pvp/merge/full'
def hist(rel):
    b = os.path.basename(rel); st = os.path.splitext(b)[0]; L = set()
    for p in [f'{S}/{rel}'] + [p for p in glob.glob(f'{S}/old_versions/*') if os.path.basename(p).startswith((st + '-', b + '-'))]:
        L |= set(l.strip() for l in open(p, errors='replace'))
    return L
for dp, dn, fn in os.walk(F):
    for f in fn:
        rel = os.path.relpath(os.path.join(dp, f), F)
        if not rel.endswith(('.c', '.h', '.inc')) or not os.path.exists(f'{S}/{rel}'): continue
        a = open(f'{S}/{rel}', errors='replace').read().splitlines(); b = open(f'{F}/{rel}', errors='replace').read().splitlines()
        if a == b: continue
        rem = [l for l in difflib.unified_diff(a, b, n=0, lineterm='') if l.startswith('-') and not l.startswith('---')]
        add = [l[1:] for l in difflib.unified_diff(a, b, n=0, lineterm='') if l.startswith('+') and not l.startswith('+++')]
        H = hist(rel); old = [l for l in add if l.strip() and l.strip() in H]
        print(f'== {rel}: removed {len(rem)}, added {len(add)} (of which {len(old)} are old src lines)')
        if '-v' in sys.argv:
            for l in rem: print('   ' + l[:170])
            for l in old: print('   OLD+ ' + l[:165])
