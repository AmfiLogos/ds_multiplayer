#!/usr/bin/env python3
"""resync_times.py HOST GUEST: per full resync in the last run of a pair (rig.log after the last 'rig start pid'):
DESYNC (guest) -> both loaded (load done) -> both running (first trace line with 'pau 0' on each copy)."""
import os, sys
INST = os.path.expanduser('~/ds_multiplayer/instances')
def lines(x):
    L = open(os.path.join(INST, x, 'rig.log'), errors='replace').read().splitlines()
    i = max(k for k, l in enumerate(L) if 'rig start pid' in l)
    out = []
    for l in L[i:]:
        try: h, m, s = l[:12].split(':'); out.append((int(h) * 3600 + int(m) * 60 + float(s), l[13:]))
        except Exception: pass
    return out
H, G = lines(sys.argv[1]), lines(sys.argv[2])
asks = [t for t, l in G if 'asking the host for its save (full resync' in l]
for a in asks:
    d = max(t for t, l in G if t <= a and 'DESYNC:' in l)
    ld = [next(t for t, l in X if t > a and 'mp_sync.json deleted (load done)' in l) for X in (H, G)]
    rn = [next(t for t, l in X if t > max(ld) - 1 and 'mp: trace' in l and ' pau 0 ' in l) for X in (H, G)]
    rd = [next(t for t, l in X if t > a and 'ready again: local speed set to 1 paused' in l) for X in (H, G)]
    print(f'DESYNC->both loaded {max(ld)-d:.2f} s (host {ld[0]-d:.2f}, guest {ld[1]-d:.2f}); both ready -> both running {max(rn)-max(rd):.2f} s; DESYNC->both running {max(rn)-d:.2f} s')
