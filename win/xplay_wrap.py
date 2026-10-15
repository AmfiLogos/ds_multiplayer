#!/usr/bin/env python3
"""xplay_run.py <pair> xplay_wrap.py [--math] <target.py> [args]: run a src/ui_test pair test cross-play with its
frame folders (OUT / SHOTS) moved to win/instances/xplay_shots, so other lanes' frames are never overwritten.
--pin-dylib PATH: the Mac copies use this dylib (a frozen release copy; build/dsrig.dylib is rebuilt by other lanes)."""
import os, sys, importlib
sys.path.insert(0, os.path.expanduser('~/ds_multiplayer/rig')); import rig
PIN = None
if '--pin-dylib' in sys.argv:
    i = sys.argv.index('--pin-dylib'); PIN = sys.argv[i + 1]; del sys.argv[i:i + 2]
tgt = next(a for a in sys.argv[1:] if a.endswith('.py')); sys.argv.remove(tgt)
sys.path.insert(0, os.path.abspath(os.path.dirname(tgt)))
mod = importlib.import_module(os.path.splitext(os.path.basename(tgt))[0])
SH = os.path.expanduser('~/ds_multiplayer/win/instances/xplay_shots'); os.makedirs(SH, exist_ok=True)
for m in [mod] + [v for v in vars(mod).values() if type(v) is type(mod) and hasattr(v, 'SHOTS')]:
    for k in ('OUT', 'SHOTS'):
        if hasattr(m, k): setattr(m, k, SH)
def main():
    if PIN: rig.DYLIB = PIN
    print('Mac dylib', rig.DYLIB, flush=True)
    return mod.main()
