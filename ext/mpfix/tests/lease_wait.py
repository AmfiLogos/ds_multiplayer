#!/usr/bin/env python3
"""lease_wait.py <lane> [seconds]: take a pair lease for <lane> (no pid: kept until released or 2 h unused), waiting for a slot."""
import os, sys, time
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig')); import rig
lane, t0 = sys.argv[1], time.time()
while time.time() - t0 < float(sys.argv[2] if len(sys.argv) > 2 else 550):
    p = rig.lease(lane)
    if p: print('leased', p); sys.exit(0)
    time.sleep(5)
print('no free pair'); sys.exit(1)
