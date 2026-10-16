#!/usr/bin/env python3
"""Run a rig test script on a given dylib: run_on.py <dylib> [--lease LANE] <script.py> [args].
--lease: take a pair first (waits for one), export RIG_PAIR / MP2_PAIR, release after. Without it the script leases itself."""
import os, sys, time, runpy
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig')); import rig
a = sys.argv[1:]; rig.DYLIB = os.path.abspath(a.pop(0)); lane = None
if a[0] == '--lease': a.pop(0); lane = a.pop(0)
pair = None
if lane:
    for _ in range(120):
        pair = rig.lease(lane, os.getpid())
        if pair: break
        time.sleep(30)
    if not pair: sys.exit('no free pair')
    os.environ['RIG_PAIR'] = os.environ['MP2_PAIR'] = pair
    print('pair', pair, 'dylib', rig.DYLIB, flush=True)
script = os.path.abspath(a[0]); sys.argv = a; sys.path.insert(0, os.path.dirname(script))
try: runpy.run_path(script, run_name='__main__')
finally:
    if pair: rig.release(pair, lane)
