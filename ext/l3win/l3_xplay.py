#!/usr/bin/env python3
"""xplay_run.py with the Windows copy W = the LOADER kit install (l3w.py: baselib.dll proxy = mod loader, multiplayer.dll
module, started through "Dungeon Settlers Modded.exe") instead of win/instances/W + C:\\DSW.
  l3_xplay.py <pair> [script.py] [--math] [script options]   (lease the pair first: rig.py lease l3win)"""
import os, sys
H = os.path.expanduser("~"); HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, f"{H}/ds_multiplayer/win"); sys.path.insert(0, f"{H}/ds_multiplayer/rig")
import wrig, l3w
wrig.PORT = l3w.PORT
wrig.INST = l3w.PROF
wrig.cmd = l3w.cmd
wrig.stop = l3w.stop
def _start():
    r = l3w.start([])
    if r: raise RuntimeError("l3w start refused (another wine process)")
wrig.start = _start
sys.argv = [f"{H}/ds_multiplayer/win/xplay_run.py"] + sys.argv[1:]
exec(compile(open(sys.argv[0]).read(), sys.argv[0], "exec"), {"__name__": "__main__", "__file__": sys.argv[0]})
