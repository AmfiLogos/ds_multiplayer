#!/usr/bin/env python3
"""merge_prep.py: 3-way merge of the FE lane source (ext/foreign_expedition/src) onto the current ~/ds_multiplayer/src.
For each lane file the base = the old src version (src/old_versions, rig/old_versions/v10_src-*) closest to the lane file.
Output: ext/foreign_expedition/merge/tree/<file> (merged), merge/report.txt (base per file, conflicts). Touches nothing in src."""
import glob, os, subprocess, sys, difflib
R = os.path.expanduser('~/ds_multiplayer'); L = os.path.join(R, 'ext/foreign_expedition/src'); S = os.path.join(R, 'src')
OUT = os.path.join(R, 'ext/foreign_expedition/merge'); T = os.path.join(OUT, 'tree'); os.makedirs(T, exist_ok=True)
def nd(a, b):
    return sum(1 for l in difflib.unified_diff(open(a, errors='replace').read().splitlines(), open(b, errors='replace').read().splitlines(), n=0) if l[:1] in '+-' and l[:3] not in ('+++', '---'))
files = [os.path.relpath(p, L) for p in glob.glob(L + '/**/*', recursive=True) if os.path.isfile(p) and p.endswith(('.c', '.h', '.sh', '.inc', 'Makefile')) and '/old' not in p and '/build' not in p]
SKIP = {'ui_tip.c'}                                  # FE never changed it: the lane copy is just older than src
rep = []
for f in sorted(files):
    mine, cur = os.path.join(L, f), os.path.join(S, f)
    os.makedirs(os.path.dirname(os.path.join(T, f)), exist_ok=True)
    if not os.path.exists(cur):
        subprocess.run(['cp', mine, os.path.join(T, f)]); rep.append(f'NEW   {f}'); continue
    if nd(mine, cur) == 0 or f in SKIP: continue
    d, b = os.path.split(f); stem, ext = os.path.splitext(b)
    cands = glob.glob(os.path.join(R, 'rig/old_versions/v10_src-*', f)) + glob.glob(os.path.join(S, d, 'old_versions', b + '-*')) + glob.glob(os.path.join(S, d, 'old_versions', stem + '-*' + ext)) + glob.glob(os.path.join(S, 'old_versions', b + '-*')) + glob.glob(os.path.join(S, 'old_versions', stem + '-*' + ext))
    cands = sorted(set(cands))
    if not cands: rep.append(f'NOBASE {f} (lane vs src {nd(mine, cur)} lines)'); continue
    base = min(cands, key=lambda c: nd(mine, c)); nb = nd(mine, base)
    if nd(cur, base) == 0 or nb == 0:
        pass
    r = subprocess.run(['git', 'merge-file', '-p', '-L', 'src', '-L', 'base', '-L', 'fe', cur, base, mine], capture_output=True)
    open(os.path.join(T, f), 'wb').write(r.stdout)
    rep.append(f"{'CONFL' if r.returncode else 'OK   '} {f}  base {os.path.relpath(base, R)} (fe vs base {nb}, src vs base {nd(cur, base)}){'  conflicts ' + str(r.returncode) if r.returncode else ''}")
open(os.path.join(OUT, 'report.txt'), 'w').write('\n'.join(rep) + '\n'); print('\n'.join(rep))
