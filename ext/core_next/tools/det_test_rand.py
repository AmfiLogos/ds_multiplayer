#!/usr/bin/env python3
"""rig/det_test.py on a LEASED pair with the lane_rand dylib and ranges (core_next lane_rand, 2026-10-15).

  det_test_rand.py [det_test.py options]     e.g. --ticks 6000

Leases a pair (lane core_next_rand), maps det_test's copies A/B onto it, sets rig.DYLIB to
ext/core_next/build/dsrig_rand.dylib and DSRIG_RANGES to ext/core_next/build/det_ranges.tsv,
passes the pair's instance dirs to desync_diff.py, releases the pair at the end."""
import os, subprocess, sys
H = os.path.expanduser('~/ds_multiplayer')
sys.path.insert(0, os.path.join(H, 'rig'))
import rig

rig.DYLIB = os.path.join(H, 'build/merge_corenext.dylib')
os.environ['DSRIG_RANGES'] = os.path.join(H, 'ext/core_next/build/det_ranges.tsv')
pair = rig.lease('corenext_rand', os.getpid())
if not pair:
    print('no pair leased (release.lock or 2 pairs in use)', flush=True); raise SystemExit(3)
print('pair', pair, 'dylib', rig.DYLIB, 'ranges', os.environ['DSRIG_RANGES'], flush=True)
try:
    for i in pair:
        if rig.alive(rig.read_pid(i)):
            raise SystemExit(f'{i} is still running (pid {rig.read_pid(i)}): not mine')
    import time as _t
    moved = []                       # a host/client mp.json left by another lane's test starts an mp session: set aside
    for i in pair:
        mj = os.path.join(rig.idir(i), 'mp.json')
        if os.path.exists(mj):
            dst = os.path.join(H, 'ext/core_next/old', f'mp_json_{i}_{_t.strftime("%H%M%S")}.json'); os.rename(mj, dst); moved.append((dst, mj))
    M = {'A': pair[0], 'B': pair[1]}
    if pair != 'AB':
        for fn in ('start', 'send', 'wait_for', 'stop', 'idir'):
            orig = getattr(rig, fn)
            setattr(rig, fn, (lambda o: lambda x, *a, **k: o(M.get(x, x), *a, **k))(orig))
    import det_test
    _run = det_test.subprocess.run
    def run(cmd, *a, **k):
        if any(str(c).endswith('desync_diff.py') for c in cmd):
            cmd = list(cmd) + [rig.idir('A'), rig.idir('B')]
        return _run(cmd, *a, **k)
    det_test.subprocess.run = run
    det_test.main()
finally:
    for dst, mj in locals().get('moved', []):
        if not os.path.exists(mj): os.rename(dst, mj)
    print('released', pair, rig.release(pair, 'corenext_rand'), flush=True)
