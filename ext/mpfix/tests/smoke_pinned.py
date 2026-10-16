#!/usr/bin/env python3
"""rig smoke on a given dylib with a leased pair (lane mpfix-ui): smoke_pinned.py <dylib> [lane]. Prints 'smoke N/3'."""
import contextlib, io, os, sys
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig')); import rig
rig.DYLIB = os.path.abspath(sys.argv[1]); lane = sys.argv[2] if len(sys.argv) > 2 else 'mpfix-ui'
import time
pair = None
for _ in range(80):                                  # max 4 pairs machine-wide: wait for one (40 min)
    pair = rig.lease(lane, os.getpid())
    if pair: break
    time.sleep(30)
if not pair: sys.exit('no free pair')
buf = io.StringIO()
try:
    with contextlib.redirect_stdout(buf): rig.smoke(pair)
finally:
    rig.release(pair, lane)
out = buf.getvalue()
od = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'out'); os.makedirs(od, exist_ok=True)
open(os.path.join(od, 'smoke_last.log'), 'w').write(out)
print('pair', pair, 'dylib', rig.DYLIB)
print('smoke %d/3' % out.count("fp_tick_advanced': True"), '| prefs:', [l for l in out.splitlines() if 'prefs identical' in l])
