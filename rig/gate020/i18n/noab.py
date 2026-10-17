#!/usr/bin/env python3
"""noab.py <script> <args...>: run a test script with rig.lease patched so it never keeps pair AB (owned by another lane)."""
import os, runpy, sys
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig
_lease, _release = rig.lease, rig.release
alias = {}
def lease(lane, *a, **k):
    p = _lease(lane, *a, **k)
    if p != 'AB': return p
    q = _lease(lane + '-x', *a, **k)          # AB stays held meanwhile, so this is another pair
    _release('AB', lane)
    if q: alias[q] = lane + '-x'
    return q
def release(pair, lane, *a, **k): return _release(pair, alias.get(pair, lane), *a, **k)
rig.lease, rig.release = lease, release
script = sys.argv[1]; sys.argv = sys.argv[1:]
runpy.run_path(script, run_name='__main__')
