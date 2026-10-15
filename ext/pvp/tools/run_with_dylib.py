#!/usr/bin/env python3
"""run_with_dylib.py <dylib> <script.py> [args...]: run a rig test script with rig.DYLIB pointed at <dylib>."""
import os, runpy, sys
RIG = os.path.expanduser('~/ds_multiplayer/rig')
sys.path.insert(0, RIG)
import rig
rig.DYLIB = os.path.abspath(sys.argv[1])
_lease = rig.lease                                       # leases go under the pvp prefix (rig/reserve.lock)
rig.lease = lambda lane, *a, **k: _lease(lane if lane.startswith('pvp') else 'pvp_' + lane, *a, **k)
if os.path.basename(sys.argv[2]) == 'rig.py':       # rig.py itself: call the imported module (its DYLIB is the patched one)
    sys.argv = ['rig.py'] + sys.argv[3:]
    print('dylib', rig.DYLIB, flush=True); rig.main(); sys.exit(0)
script = os.path.abspath(sys.argv[2])
sys.argv = [script] + sys.argv[3:]
sys.path.insert(0, os.path.dirname(script))
print('dylib', rig.DYLIB, flush=True)
runpy.run_path(script, run_name='__main__')
