#!/usr/bin/env python3
"""g11_wml.py lock|unlock|install|run <out>|off <out>: MelonLoader on Windows (melon's wml_test checks) with the GATED kit:
install = a copy of ~/ds_launcher/loader/build/win/kit + the test mod hello-melon in files/mods (never shipped), run
through install.bat in the DSMP bottle. Wine lock owner 'gate13' (wml_test's own install builds melon's old worktree)."""
import os, shutil, subprocess, sys, time
H = os.path.expanduser('~'); sys.path.insert(0, f'{H}/ds_multiplayer/ext/melon/tools'); sys.path.insert(0, f'{H}/ds_multiplayer/win')
import wml_test as t
LOCK = f'{H}/ds_multiplayer/rig/wine.lock'
t.owned = lambda: os.path.exists(f'{LOCK}/owner') and open(f'{LOCK}/owner').read().strip() == 'gate13'
def lock():
    busy = subprocess.run(['/bin/sh', '-c', "ps -axo etime=,comm= | grep -iE 'wine|\\.exe$' | grep -ve '-[0-9][0-9]:'"], capture_output=True, text=True).stdout.strip()
    if busy: sys.exit('wine busy: ' + busy)
    os.mkdir(LOCK); open(f'{LOCK}/owner', 'w').write('gate13\n'); print('wine lock taken', flush=True)
def unlock():
    if t.owned():
        os.makedirs(f'{H}/ds_multiplayer/rig/old', exist_ok=True)
        os.replace(f'{LOCK}/owner', f'{H}/ds_multiplayer/rig/old/wine.lock.owner.gate13.{int(time.time())}'); os.rmdir(LOCK); print('wine lock released')
def install():
    assert t.owned()
    k = f'{H}/ds_mp_gate13/ext/gate13/out/wml_kit'
    if os.path.exists(k): os.makedirs(f'{H}/ds_mp_gate13/ext/gate13/out/old', exist_ok=True); shutil.move(k, f'{H}/ds_mp_gate13/ext/gate13/out/old/wml_kit.{int(time.time())}')
    shutil.copytree(f'{H}/ds_launcher/loader/build/win/kit', k)
    shutil.copytree(f'{t.M}/plugin/mods/hello-melon', f'{k}/files/mods/hello-melon')
    rc = t.w.install(k); print('install rc', rc, flush=True)
    subprocess.run([f'{t.w.C}/wine', '--bottle', t.w.BOTTLE, 'reg', 'add', 'HKCU\\Software\\Wine\\AppDefaults\\DungeonSettlers.exe\\DllOverrides',
                    '/v', 'version', '/d', 'native,builtin', '/f'], capture_output=True)
    return rc
a = sys.argv[1:]
if a[0] == 'lock': lock()
elif a[0] == 'unlock': unlock()
elif a[0] == 'install': sys.exit(install())
elif a[0] == 'run': t.run(os.path.expanduser(a[1]), int(a[2]) if len(a) > 2 else 540, True)
elif a[0] == 'off': t.run(os.path.expanduser(a[1]), 300, False)
