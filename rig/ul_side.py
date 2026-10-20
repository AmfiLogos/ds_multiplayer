#!/usr/bin/env python3
"""Run a pair test with Units Laboratory (custom-modes.dylib next to the dylib) on ONE copy of the pair only.

  python3 rig/ul_side.py host|guest|none|both <test.py> [test args...]

host = only the leased pair's first copy loads Units Laboratory, guest = only the second, none = neither, both = both
(the test's own default). The test imports ~/ds_multiplayer/rig/rig.py; this wraps its lease() (to learn the pair) and
start() (DSRIG_CUSTOM=0 for a copy without Units Laboratory: src/rig_main.c custom_modes_start) in that module, then
runs the test in this process. Since the Units Laboratory split nothing of a multiplayer session needs it."""
import os, runpy, sys

side, test = sys.argv[1], os.path.abspath(sys.argv[2])
assert side in ('host', 'guest', 'none', 'both'), side
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig
_lease, _start, pair = rig.lease, rig.start, []
def lease(*a, **k):
    p = _lease(*a, **k)
    if p: pair[:] = list(p)
    return p
def start(x, *a, **k):
    on = side == 'both' or (side == 'host' and pair and x == pair[0]) or (side == 'guest' and pair and x == pair[1])
    if on: os.environ.pop('DSRIG_CUSTOM', None)
    else: os.environ['DSRIG_CUSTOM'] = '0'
    print(f'ul_side: copy {x} {"WITH" if on else "without"} Units Laboratory', flush=True)
    return _start(x, *a, **k)
rig.lease, rig.start = lease, start
sys.argv = [test] + sys.argv[3:]
sys.path.insert(0, os.path.dirname(test))
runpy.run_path(test, run_name='__main__')
