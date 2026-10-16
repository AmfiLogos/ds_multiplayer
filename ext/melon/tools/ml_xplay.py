#!/usr/bin/env python3
"""l3_xplay.py (Mac host + Windows guest through the loader kit) for lane melon: the Windows copy is started by
wml_test.start (no 3-minute title watchdog; wine output kept in build.noindex/wml_wine.out; WINEDEBUG passes through).
  ml_xplay.py <pair> [script.py] [--math] [options]   (pair leased and wine.lock taken by the caller)"""
import os, sys
H = os.path.expanduser("~")
sys.path.insert(0, f"{H}/ds_multiplayer/ext/l3win"); sys.path.insert(0, f"{H}/ds_multiplayer/win"); sys.path.insert(0, f"{H}/ds_multiplayer/rig")
sys.path.insert(0, f"{H}/ds_multiplayer/ext/melon/tools")
import wrig, l3w, wml_test
wrig.INST = l3w.PROF; wrig.cmd = l3w.cmd; wrig.stop = l3w.stop
wrig.start = wml_test.start
sys.argv = [f"{H}/ds_multiplayer/win/xplay_run.py"] + sys.argv[1:]
exec(compile(open(sys.argv[0]).read(), sys.argv[0], "exec"), {"__name__": "__main__", "__file__": sys.argv[0]})
