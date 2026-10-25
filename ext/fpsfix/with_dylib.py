#!/usr/bin/env python3
"""with_dylib.py <dylib> <test.py> [args]: run a rig test with rig.DYLIB set to <dylib>."""
import os, runpy, sys
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig'))
import rig
rig.DYLIB = os.path.abspath(sys.argv[1]); t = sys.argv[2]; sys.argv = [t] + sys.argv[3:]
runpy.run_path(t, run_name='__main__')
