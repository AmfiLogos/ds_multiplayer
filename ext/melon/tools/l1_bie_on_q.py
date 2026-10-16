#!/usr/bin/env python3
"""Run loader/test/l1_bie_test.py (BepInEx through the loader) on lane melon's test copy Q with this worktree's dsload,
under a pair lease (the copy counts as a pair).  usage: l1_bie_on_q.py <out>"""
import os, subprocess, sys, time
W = os.path.expanduser('~/ds_launcher_melon/loader/test'); sys.path.insert(0, W)
import l1_test as t
t.X = 'Q'; t.I = t.r.idir('Q'); t.SDIR = os.path.join(t.I, 'softrestart'); t.MODS = os.path.join(t.I, 'data', 'Mods')
RIG = os.path.expanduser('~/ds_multiplayer/rig/rig.py'); lease = None; t0 = time.time()
while not lease and time.time() - t0 < 1200:
    r = subprocess.run([sys.executable, RIG, 'lease', 'melon', '--pid', str(os.getpid())], capture_output=True, text=True)
    lease = r.stdout.strip().splitlines()[-1] if r.returncode == 0 and r.stdout.strip() else None
    if not lease: time.sleep(15)
print('lease', lease, flush=True)
if not lease: sys.exit('no lease')
try:
    sys.argv = ['l1_bie_test.py', sys.argv[1]]
    import l1_bie_test
    l1_bie_test.ENV['DSML_BEPINEX_SCRIPT'] = os.path.expanduser('~/ds_launcher_melon/app/bepinex/install_bepinex.sh')   # this branch's pin
    print('installer', l1_bie_test.ENV['DSML_BEPINEX_SCRIPT'], flush=True)
    l1_bie_test.main()
finally:
    subprocess.run([sys.executable, RIG, 'release', lease, 'melon'])
