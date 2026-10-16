#!/usr/bin/env python3
"""ld_run.py <test.py> <out> [args...]: one loader copy test of ~/ds_launcher/loader/test under a gate13 pair lease
(the copy counts as a pair; DSRIG_RELEASE=1 while the gate holds rig/release.lock). l1_bie_test gets the shipped
BepInEx installer (~/ds_launcher/app/bepinex/install_bepinex.sh = the release pin) instead of its old default.
uikit_test: install() of the gated build, no build(). Tests that lease for themselves (l1_ml_test) run without an extra lease: pass --no-lease."""
import os, runpy, subprocess, sys, time
LD = os.path.expanduser(os.environ.get('LD', '~/ds_launcher'))
T = os.path.join(LD, 'loader', 'test'); RIG = os.path.expanduser('~/ds_multiplayer/rig/rig.py')
os.environ['DSRIG_RELEASE'] = '1'
args = sys.argv[1:]; own = '--no-lease' not in args; args = [a for a in args if a != '--no-lease']
test, rest = args[0], args[1:]
lease = None
if own:
    t0 = time.time()
    while not lease and time.time() - t0 < 600:
        r = subprocess.run([sys.executable, RIG, 'lease', 'gate13', '--pid', str(os.getpid())], capture_output=True, text=True)
        lease = r.stdout.strip().splitlines()[-1] if r.returncode == 0 and r.stdout.strip() else None
        if not lease: time.sleep(15)
    print('lease', lease, flush=True)
    if not lease: sys.exit('no lease')
os.chdir(os.path.join(LD, 'loader')); sys.path.insert(0, T)
rc = 0
try:
    sys.argv = [os.path.join(T, test)] + rest
    if test == 'l1_bie_test.py':
        import l1_bie_test as m
        m.ENV['DSML_BEPINEX_SCRIPT'] = os.path.join(LD, 'app', 'bepinex', 'install_bepinex.sh')
        print('installer', m.ENV['DSML_BEPINEX_SCRIPT'], flush=True)
        m.main()
    elif test == 'uikit_test.py':                         # install the gated loader into copy L, but never rebuild it
        import uikit_test as m
        LW = m.LW; d = os.path.join(LW, 'build', 'test_mods', 'uikit-demo'); os.makedirs(d, exist_ok=True)   # test mod only
        m.sh(f'cd "{LW}" && nice -n 19 clang -arch arm64 -O2 -Wall -Isdk -dynamiclib -framework CoreFoundation -framework CoreGraphics -lobjc '
             f'-install_name @rpath/uikit-demo.dylib -o "{d}/uikit-demo.dylib.tmp" test/mods_src/uikit-demo/uikit_demo.c '
             f'&& codesign -f -s - "{d}/uikit-demo.dylib.tmp" 2>/dev/null; mv -f "{d}/uikit-demo.dylib.tmp" "{d}/uikit-demo.dylib"')
        import shutil
        shutil.copy2(os.path.join(LW, 'test/mods_src/uikit-demo/mod.json'), d); shutil.copy2(os.path.join(LW, 'modules/modmenu/slogel.png'), d)
        m.install(); m.main()
    else:
        runpy.run_path(os.path.join(T, test), run_name='__main__')
except SystemExit as e:
    rc = e.code if isinstance(e.code, int) else (0 if e.code is None else 1)
finally:
    if lease: subprocess.run([sys.executable, RIG, 'release', lease, 'gate13'])
sys.exit(rc)
