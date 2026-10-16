#!/usr/bin/env python3
"""v13t_run.py <args of loader/test/soft/v13_test.py>: the soft-restart Apply timing test under the gate's own
rig/release.lock (the test waits while the lock exists; the gate holds it, so the test is told it is absent). Leases still
go through DSRIG_RELEASE=1. cwd = ~/ds_launcher/loader."""
import os, runpy, sys
os.environ['DSRIG_RELEASE'] = '1'
LOCK = os.path.expanduser('~/ds_multiplayer/rig/release.lock'); _ex = os.path.exists
os.path.exists = lambda p: False if os.path.abspath(os.fspath(p)) == LOCK else _ex(p)
T = os.path.expanduser('~/ds_launcher/loader/test/soft/v13_test.py'); sys.argv = [T] + sys.argv[1:]
sys.path.insert(0, os.path.dirname(T)); runpy.run_path(T, run_name='__main__')
