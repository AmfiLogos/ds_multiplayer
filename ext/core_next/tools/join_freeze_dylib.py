#!/usr/bin/env python3
"""lane_join: run rig/join_freeze_test.py with another dylib (and optional DSRIG_* env), lease name core_join.

  join_freeze_dylib.py [--dylib PATH] [--env K=V ...] [--joins 3]
"""
import os, sys
RIG = os.path.expanduser('~/ds_multiplayer/rig')
sys.path.insert(0, RIG)
import rig
a = sys.argv
if '--dylib' in a: rig.DYLIB = os.path.abspath(os.path.expanduser(a[a.index('--dylib') + 1]))
for i, x in enumerate(a):
    if x == '--env': k, v = a[i + 1].split('=', 1); os.environ[k] = v
_lease = rig.lease
rig.lease = lambda lane, pid=None: _lease('corenext_join', pid)
_release = rig.release
rig.release = lambda pair, lane=None: _release(pair, lane or 'corenext_join')   # night rule 10
import join_freeze_test
print('dylib', rig.DYLIB, flush=True)
join_freeze_test.main()
