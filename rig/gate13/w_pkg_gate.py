#!/usr/bin/env python3
"""w_pkg_gate.py <release dir> <out>: loader's w_pkg_test (Windows, DSMP bottle: a mod zip dropped while the game runs
is installed, listed, turned on by Apply) on the GATED kit instead of a fresh build: a copy of
~/ds_launcher/loader/build/win/kit without gcd-cdr (the mod the test drops), installed with install.bat; the dropped zip
is the release's gcd-cdr zip. Wine lock owner gate13. Prints the test's "x/y checks" line."""
import os, shutil, sys, time
H = os.path.expanduser('~'); sys.path.insert(0, f'{H}/ds_launcher/loader/test')
REL, OUT = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
os.environ['PKG_ZIP'] = os.path.join(REL, 'gcd-cdr-2.0.0.zip')
import w_pkg_test as t
t.PKG = os.environ['PKG_ZIP']
LOCK = t.LOCK
t.owned = lambda: os.path.exists(f'{LOCK}/owner') and open(f'{LOCK}/owner').read().strip() == 'gate13'
def install():
    k = os.path.join(OUT, 'kit')
    if os.path.exists(k): shutil.move(k, os.path.join(OUT, f'kit.old.{int(time.time())}'))
    shutil.copytree(f'{H}/ds_launcher/loader/build/win/kit', k)
    shutil.move(os.path.join(k, 'files', 'mods', 'gcd-cdr'), os.path.join(OUT, f'gcd-cdr.removed.{int(time.time())}'))
    return t.w.install(k)
os.makedirs(OUT, exist_ok=True)
busy = os.popen("ps -axo etime=,comm= | grep -iE 'wine|\\.exe$' | grep -ve '-[0-9][0-9]:'").read().strip()
if busy: sys.exit('wine busy: ' + busy)
os.mkdir(LOCK); open(f'{LOCK}/owner', 'w').write('gate13\n')
try:
    root = t.w.ROOT
    if os.path.exists(root): os.rename(root, root + '.gate13-' + time.strftime('%Y%m%d-%H%M%S'))   # clean install
    rc = install(); print('install rc', rc, flush=True)
    if rc == 0: t.run(os.path.join(OUT, 'run'))
finally:
    os.makedirs(f'{H}/ds_multiplayer/rig/old', exist_ok=True)
    os.replace(f'{LOCK}/owner', f'{H}/ds_multiplayer/rig/old/wine.lock.owner.gate13.{int(time.time())}'); os.rmdir(LOCK)
