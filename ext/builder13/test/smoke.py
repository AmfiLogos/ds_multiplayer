#!/usr/bin/env python3
"""rig smoke (3 copies) with this lane's Custom Modes build on main's shared dsrig.dylib, on a leased pair.
  smoke.py [custom-modes.dylib]   (default: build/merge_builder13/custom-modes.dylib of this worktree)"""
import os, subprocess, sys, time
WT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
RIG = os.path.expanduser('~/ds_multiplayer/rig/rig.py')
dl = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(WT, 'build/merge_builder13/custom-modes.dylib')
t0 = time.time()
while True:
    p = subprocess.run([sys.executable, RIG, 'lease', 'builder13', '--pid', str(os.getpid())], capture_output=True, text=True)
    if not p.returncode: break
    if time.time() - t0 > 1200: sys.exit('no pair: ' + (p.stderr or p.stdout))
    time.sleep(20)
pair = p.stdout.strip().split()[-1]
print('lease', pair, 'custom', dl, flush=True)
try:
    r = subprocess.run([sys.executable, RIG, 'smoke'], env=dict(os.environ, DSRIG_CUSTOM=dl, RIG_PAIR=pair), capture_output=True, text=True)   # X alone, then the pair = 3 copies
    out = r.stdout + r.stderr
    open(os.path.join(WT, 'ext/builder13/out/smoke_%s.log' % time.strftime('%Y%m%d-%H%M%S')), 'w').write(out)
    ok = out.count("'fp_tick_advanced': True")
    print('smoke: %d/3 fp_tick_advanced; prefs lines:' % ok, [l for l in out.splitlines() if 'prefs identical' in l])
finally:
    subprocess.run([sys.executable, RIG, 'release', pair, 'builder13'], capture_output=True, text=True)
    print('released', pair)
