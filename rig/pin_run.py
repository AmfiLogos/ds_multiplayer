#!/usr/bin/env python3
"""pin_run.py <dylib> <script.py> [args]: run a rig test's main() with rig.DYLIB pinned to <dylib> (a frozen release copy)
for scripts that have no --dylib option (desync_partial_test.py, ux_core_pair.py). Used by release_gate.sh."""
import importlib, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import rig
dl, tgt = sys.argv[1], sys.argv[2]; sys.argv = [tgt] + sys.argv[3:]
sys.path.insert(0, os.path.dirname(os.path.abspath(tgt)))
mod = importlib.import_module(os.path.splitext(os.path.basename(tgt))[0])
rig.DYLIB = dl
print('pinned dylib', rig.DYLIB, flush=True)
mod.main()
