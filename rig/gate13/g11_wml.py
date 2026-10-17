#!/usr/bin/env python3
"""g11_wml.py lock|unlock|install|run <out>|off <out>: MelonLoader on Windows (melon's wml_test checks) with the GATED kit:
install = a copy of ~/ds_launcher/loader/build/win/kit + the test mod hello-melon in files/mods (never shipped), run
through install.bat in the DSMP bottle. Wine lock owner 'gate13' (wml_test's own install builds melon's old worktree)."""
import os, shutil, subprocess, sys, time
H = os.path.expanduser('~'); sys.path.insert(0, f'{H}/ds_multiplayer/ext/melon/tools'); sys.path.insert(0, f'{H}/ds_multiplayer/win')
import wml_test as t
LOCK, WL = f'{H}/ds_multiplayer/rig/wine.lock', f'{H}/ds_multiplayer/rig/wine_lease.py'
t.owned = lambda: os.path.exists(f'{LOCK}/owner') and open(f'{LOCK}/owner').read().split('\n')[0].strip() == 'gate13'
def lock():
    if subprocess.run([sys.executable, WL, 'take', 'gate13', '--pid', str(os.getppid())]).returncode: sys.exit('wine busy / locked')
def unlock():
    if t.owned():
        subprocess.run([sys.executable, WL, 'release', 'gate13'])
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
